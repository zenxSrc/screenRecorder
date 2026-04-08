/**
 * ╔══════════════════════════════════════════════════════════════╗
 * ║          LINUX SCREEN RECORDER  v1.01                       ║
 * ║          Industrial-Grade TUI • Keyboard & Mouse Support    ║
 * ║          © 2026 — MIT License                               ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * Build:
 *   g++ -std=c++17 -O2 -o screen_recorder screen_recorder.cpp \
 *       -lncurses -lpanel -lmenu -lform -lpthread
 *
 * Dependencies:  ncurses, ffmpeg, PulseAudio (pactl), xdpyinfo or xrandr
 * Install:       sudo apt install libncurses-dev ffmpeg pulseaudio-utils
 */

#include <ncurses.h>
#include <menu.h>
#include <panel.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Version & constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr const char* VERSION       = "1.01";
static constexpr const char* APP_NAME      = "Linux Screen Recorder";
static constexpr int         MIN_ROWS      = 30;
static constexpr int         MIN_COLS      = 72;
static constexpr long        MAX_PATH_LEN  = 4096;
static constexpr size_t      MAX_FNAME_LEN = 255;

// ─────────────────────────────────────────────────────────────────────────────
//  Color pair IDs
// ─────────────────────────────────────────────────────────────────────────────
enum ColorPair : int {
    CP_NORMAL    = 1,
    CP_TITLE     = 2,
    CP_HIGHLIGHT = 3,
    CP_ERROR     = 4,
    CP_SUCCESS   = 5,
    CP_WARNING   = 6,
    CP_DIM       = 7,
    CP_HOT       = 8,
    CP_REC       = 9,
    CP_BOX       = 10,
    CP_BTN       = 11,
    CP_BTN_SEL   = 12,
};

// ─────────────────────────────────────────────────────────────────────────────
//  Logging
// ─────────────────────────────────────────────────────────────────────────────
class Logger {
public:
    enum class Level { DBG, INF, WRN, LERR };

    static Logger& instance() {
        static Logger l;
        return l;
    }

    void open(const std::string& path) {
        std::lock_guard<std::mutex> lk(mtx_);
        ofs_.open(path, std::ios::app);
    }

    void log(Level lvl, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!ofs_.is_open()) return;
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
        const char* ls[] = {"DBG  ","INFO ","WARN ","ERROR"};
        ofs_ << "[" << ts << "] [" << ls[(int)lvl] << "] " << msg << "\n";
        ofs_.flush();
    }

private:
    Logger() = default;
    std::mutex    mtx_;
    std::ofstream ofs_;
};

#define LOG_INFO(m)  Logger::instance().log(Logger::Level::INF,  m)
#define LOG_WARN(m)  Logger::instance().log(Logger::Level::WRN,  m)
#define LOG_ERR(m)   Logger::instance().log(Logger::Level::LERR,   m)
#define LOG_DEBUG(m) Logger::instance().log(Logger::Level::DBG, m)

// ─────────────────────────────────────────────────────────────────────────────
//  Safe shell exec (no shell injection — uses /bin/sh -c but args are fixed)
// ─────────────────────────────────────────────────────────────────────────────
static std::string safeExec(const std::string& cmd, int* exitCode = nullptr) {
    LOG_DEBUG("exec: " + cmd);
    std::array<char, 256> buf{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_ERR("popen failed for: " + cmd + " errno=" + std::to_string(errno));
        if (exitCode) *exitCode = -1;
        return "";
    }
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        result += buf.data();
    }
    int rc = pclose(pipe);
    if (exitCode) *exitCode = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    // Trim trailing newlines
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dependency checker
// ─────────────────────────────────────────────────────────────────────────────
struct DepCheck {
    std::string name;
    bool        present;
    std::string installHint;
};

static std::vector<DepCheck> checkDependencies() {
    std::vector<DepCheck> deps;
    auto check = [&](const std::string& binary, const std::string& pkg) {
        int rc = 0;
        safeExec("which " + binary + " >/dev/null 2>&1", &rc);
        deps.push_back({binary, rc == 0, "sudo apt install " + pkg});
    };
    check("ffmpeg",   "ffmpeg");
    check("pactl",    "pulseaudio-utils");
    check("xdpyinfo", "x11-utils");   // optional — xrandr is fallback
    return deps;
}

// ─────────────────────────────────────────────────────────────────────────────
//  System detection helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::string detectResolution() {
    // xdpyinfo
    std::string r = safeExec("xdpyinfo 2>/dev/null | awk '/dimensions:/{print $2}'");
    if (!r.empty() && r.find('x') != std::string::npos) return r;
    // xrandr fallback
    r = safeExec("xrandr 2>/dev/null | grep '*' | awk '{print $1}' | head -n1");
    if (!r.empty() && r.find('x') != std::string::npos) return r;
    LOG_WARN("Could not detect screen resolution, defaulting to 1920x1080");
    return "1920x1080";
}

static std::string getSystemAudioMonitor() {
    std::string sink = safeExec("pactl get-default-sink 2>/dev/null");
    if (!sink.empty()) return sink + ".monitor";
    std::string mon = safeExec("pactl list sources short 2>/dev/null | grep monitor | head -n1 | awk '{print $2}'");
    if (!mon.empty()) return mon;
    LOG_WARN("Could not detect PulseAudio monitor, using fallback");
    return "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor";
}

static std::vector<std::string> listPulseSinks() {
    std::string raw = safeExec("pactl list sinks short 2>/dev/null | awk '{print $2}'");
    std::vector<std::string> sinks;
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line))
        if (!line.empty()) sinks.push_back(line + ".monitor");
    if (sinks.empty()) sinks.push_back(getSystemAudioMonitor());
    return sinks;
}

static uint64_t freeDiskMB() {
    try {
        auto si = fs::space(fs::current_path());
        return si.available / (1024 * 1024);
    } catch (...) { return 0; }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Path / filename validation
// ─────────────────────────────────────────────────────────────────────────────
static std::string validateOutputPath(const std::string& raw, std::string& err) {
    err.clear();
    if (raw.empty()) return "";

    // Reject null bytes or shell-dangerous chars
    static const std::string forbidden = "|;&$`\\\"'<>{}[]()!*?~";
    for (char c : raw) {
        if (c == '\0') { err = "Path contains null byte"; return ""; }
        if (forbidden.find(c) != std::string::npos) {
            err = std::string("Forbidden character '") + c + "' in filename";
            return "";
        }
    }

    if (raw.length() > MAX_PATH_LEN) {
        err = "Path too long (max " + std::to_string(MAX_PATH_LEN) + " chars)";
        return "";
    }

    fs::path p(raw);
    if (p.filename().string().length() > MAX_FNAME_LEN) {
        err = "Filename too long (max " + std::to_string(MAX_FNAME_LEN) + " chars)";
        return "";
    }

    // Ensure .mp4 extension
    std::string s = raw;
    if (p.extension() != ".mp4") s += ".mp4";

    // Check parent directory is writable
    fs::path parent = fs::path(s).parent_path();
    if (parent.empty()) parent = fs::current_path();
    if (!fs::exists(parent)) {
        err = "Directory does not exist: " + parent.string();
        return "";
    }
    if (access(parent.string().c_str(), W_OK) != 0) {
        err = "No write permission to: " + parent.string();
        return "";
    }

    // Warn if file already exists (handled by caller)
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Recording configuration
// ─────────────────────────────────────────────────────────────────────────────
enum class ResolutionMode { R720p, R1080p, R1440p, R4K, Native };
enum class FramerateMode  { FPS24, FPS30, FPS60 };
enum class AudioMode      { None, System, CustomDevice };
enum class EncoderPreset  { Ultrafast, Fast, Medium, Slow };
enum class PixelFormat    { YUV420, YUV444 };

struct RecordingConfig {
    ResolutionMode resolution   = ResolutionMode::R1080p;
    FramerateMode  framerate    = FramerateMode::FPS60;
    AudioMode      audio        = AudioMode::System;
    std::string    audioDevice;
    std::string    outputPath   = "recording.mp4";
    EncoderPreset  preset       = EncoderPreset::Ultrafast;
    int            crf          = 23;           // 0–51; lower = higher quality
    std::string    displayStr   = ":0";
    std::string    nativeRes    = "1920x1080";
    bool           showCursor   = true;
    bool           limitDuration = false;
    int            maxSeconds   = 0;
};

static std::string resolutionStr(const RecordingConfig& cfg) {
    switch (cfg.resolution) {
        case ResolutionMode::R720p:  return "1280x720";
        case ResolutionMode::R1080p: return "1920x1080";
        case ResolutionMode::R1440p: return "2560x1440";
        case ResolutionMode::R4K:    return "3840x2160";
        case ResolutionMode::Native: return cfg.nativeRes;
    }
    return cfg.nativeRes;
}

static std::string framerateStr(FramerateMode m) {
    switch (m) {
        case FramerateMode::FPS24: return "24";
        case FramerateMode::FPS30: return "30";
        case FramerateMode::FPS60: return "60";
    }
    return "60";
}

static std::string presetStr(EncoderPreset p) {
    switch (p) {
        case EncoderPreset::Ultrafast: return "ultrafast";
        case EncoderPreset::Fast:      return "fast";
        case EncoderPreset::Medium:    return "medium";
        case EncoderPreset::Slow:      return "slow";
    }
    return "ultrafast";
}

static std::string buildFFmpegCommand(const RecordingConfig& cfg) {
    std::ostringstream cmd;
    const std::string fps = framerateStr(cfg.framerate);
    const std::string vsize = resolutionStr(cfg);
    const std::string drawmouse = cfg.showCursor ? "1" : "0";

    cmd << "ffmpeg";
    if (cfg.limitDuration && cfg.maxSeconds > 0)
        cmd << " -t " << cfg.maxSeconds;
    cmd << " -f x11grab"
        << " -framerate " << fps
        << " -thread_queue_size 8192"
        << " -video_size " << cfg.nativeRes    // capture native, scale after
        << " -draw_mouse " << drawmouse
        << " -i " << cfg.displayStr;

    if (cfg.audio != AudioMode::None) {
        cmd << " -f pulse"
            << " -thread_queue_size 4096"
            << " -i \"" << cfg.audioDevice << "\"";
    }

    // Scale only if not native
    if (cfg.resolution != ResolutionMode::Native) {
        cmd << " -vf \"scale=" << vsize << ":flags=lanczos\"";
    }

    cmd << " -c:v libx264"
        << " -preset " << presetStr(cfg.preset)
        << " -crf " << cfg.crf
        << " -pix_fmt yuv420p"
        << " -r " << fps
        << " -movflags +faststart";

    if (cfg.audio != AudioMode::None) {
        cmd << " -c:a aac -b:a 192k -ac 2 -ar 48000";
    }

    cmd << " -y \"" << cfg.outputPath << "\"";
    return cmd.str();
}

// ─────────────────────────────────────────────────────────────────────────────
//  TUI state machine
// ─────────────────────────────────────────────────────────────────────────────
enum class Screen {
    DepCheck,
    Resolution,
    Framerate,
    Audio,
    Encoder,
    Output,
    Confirm,
    Recording,
    Result,
    Help,
    Quit,
};

// ─────────────────────────────────────────────────────────────────────────────
//  TUI Application
// ─────────────────────────────────────────────────────────────────────────────
class App {
public:
    App();
    ~App();
    int run();

private:
    // ncurses helpers
    void initColors();
    void drawBorder(WINDOW* w, const std::string& title = "");
    void drawHeader(WINDOW* w, int row);
    void drawFooter(WINDOW* w, int row, const std::string& hints);
    void centerText(WINDOW* w, int row, const std::string& text, int pair, int attrs = 0);
    void statusBar(const std::string& msg, int pair = CP_DIM);

    // Screens
    Screen screenDepCheck();
    Screen screenResolution();
    Screen screenFramerate();
    Screen screenAudio();
    Screen screenEncoder();
    Screen screenOutput();
    Screen screenConfirm();
    Screen screenRecording();
    Screen screenResult(bool ok, int exitCode);
    Screen screenHelp(Screen returnTo);

    // Widget helpers
    int    drawMenu(WINDOW* w, const std::vector<std::string>& items,
                   int selected, int startRow, bool numbered = true);
    bool   inputBox(WINDOW* w, int row, int col, int width,
                   const std::string& label, std::string& value,
                   const std::string& defaultVal = "");
    bool   slider(WINDOW* w, int row, const std::string& label,
                 int& value, int minV, int maxV, int step = 1);
    bool   confirmDialog(const std::string& question);

    // State
    RecordingConfig      cfg_;
    Screen               current_   = Screen::DepCheck;
    std::string          lastError_;
    std::string          nativeRes_;
    std::vector<DepCheck> deps_;
    WINDOW*              mainWin_   = nullptr;
    int                  ROWS_      = 0;
    int                  COLS_      = 0;

    // Recording process
    pid_t                ffmpegPid_ = -1;
    std::atomic<bool>    recRunning_{false};
    std::thread          timerThread_;
    std::chrono::steady_clock::time_point recStart_;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / destructor
// ─────────────────────────────────────────────────────────────────────────────
App::App() {
    // Logger
    const char* home = getenv("HOME");
    std::string logPath = home ? std::string(home) + "/.screen_recorder.log"
                               : "/tmp/screen_recorder.log";
    Logger::instance().open(logPath);
    LOG_INFO("=== Linux Screen Recorder v" + std::string(VERSION) + " started ===");

    // ncurses init
    initscr();
    if (!has_colors()) {
        endwin();
        throw std::runtime_error("Terminal does not support colors.");
    }
    start_color();
    use_default_colors();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);
    mouseinterval(50);
    curs_set(0);
    set_escdelay(25);

    getmaxyx(stdscr, ROWS_, COLS_);
    if (ROWS_ < MIN_ROWS || COLS_ < MIN_COLS) {
        endwin();
        throw std::runtime_error(
            "Terminal too small. Minimum: " + std::to_string(MIN_COLS) +
            "×" + std::to_string(MIN_ROWS) + "  Got: " +
            std::to_string(COLS_) + "×" + std::to_string(ROWS_));
    }

    initColors();
    mainWin_ = newwin(ROWS_, COLS_, 0, 0);
    keypad(mainWin_, TRUE);

    // Detect env
    nativeRes_ = detectResolution();
    cfg_.nativeRes = nativeRes_;
    const char* disp = getenv("DISPLAY");
    cfg_.displayStr = disp ? disp : ":0";
    cfg_.audioDevice = getSystemAudioMonitor();

    LOG_INFO("Native resolution: " + nativeRes_);
    LOG_INFO("DISPLAY: " + cfg_.displayStr);
    LOG_INFO("Audio monitor: " + cfg_.audioDevice);
}

App::~App() {
    if (timerThread_.joinable()) timerThread_.join();
    if (mainWin_) delwin(mainWin_);
    endwin();
    LOG_INFO("=== Exiting cleanly ===");
}

void App::initColors() {
    init_pair(CP_NORMAL,    COLOR_WHITE,   -1);
    init_pair(CP_TITLE,     COLOR_CYAN,    -1);
    init_pair(CP_HIGHLIGHT, COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_ERROR,     COLOR_RED,     -1);
    init_pair(CP_SUCCESS,   COLOR_GREEN,   -1);
    init_pair(CP_WARNING,   COLOR_YELLOW,  -1);
    init_pair(CP_DIM,       COLOR_WHITE,   -1);
    init_pair(CP_HOT,       COLOR_MAGENTA, -1);
    init_pair(CP_REC,       COLOR_WHITE,   COLOR_RED);
    init_pair(CP_BOX,       COLOR_CYAN,    -1);
    init_pair(CP_BTN,       COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_BTN_SEL,   COLOR_WHITE,   COLOR_BLUE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing helpers
// ─────────────────────────────────────────────────────────────────────────────
void App::drawBorder(WINDOW* w, const std::string& title) {
    int cols = getmaxx(w); (void)getmaxy(w);
    wattron(w, COLOR_PAIR(CP_BOX) | A_BOLD);
    box(w, 0, 0);
    if (!title.empty()) {
        int tx = (cols - (int)title.size() - 4) / 2;
        if (tx < 1) tx = 1;
        mvwprintw(w, 0, tx, "[ %s ]", title.c_str());
    }
    wattroff(w, COLOR_PAIR(CP_BOX) | A_BOLD);
}

void App::drawHeader(WINDOW* w, int row) {
    int cols = getmaxx(w);
    wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    std::string left = std::string(" ") + APP_NAME;
    std::string right = std::string("v") + VERSION + " ";
    mvwprintw(w, row, 1, "%s", left.c_str());
    mvwprintw(w, row, cols - (int)right.size() - 1, "%s", right.c_str());
    wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    wattron(w, COLOR_PAIR(CP_DIM));
    mvwhline(w, row + 1, 1, ACS_HLINE, cols - 2);
    wattroff(w, COLOR_PAIR(CP_DIM));
}

void App::drawFooter(WINDOW* w, int row, const std::string& hints) {
    int cols = getmaxx(w);
    wattron(w, COLOR_PAIR(CP_DIM));
    mvwhline(w, row - 1, 1, ACS_HLINE, cols - 2);
    // Truncate if needed
    std::string h = hints;
    if ((int)h.size() > cols - 4) h = h.substr(0, cols - 7) + "...";
    mvwprintw(w, row, 2, "%s", h.c_str());
    wattroff(w, COLOR_PAIR(CP_DIM));
}

void App::centerText(WINDOW* w, int row, const std::string& text, int pair, int attrs) {
    int cols = getmaxx(w);
    int col = (cols - (int)text.size()) / 2;
    if (col < 1) col = 1;
    wattron(w, COLOR_PAIR(pair) | attrs);
    mvwprintw(w, row, col, "%s", text.c_str());
    wattroff(w, COLOR_PAIR(pair) | attrs);
}

void App::statusBar(const std::string& msg, int pair) {
    int cols = getmaxx(mainWin_);
    wattron(mainWin_, COLOR_PAIR(pair));
    mvwprintw(mainWin_, ROWS_ - 1, 2, "%s", msg.c_str());
    // Pad rest
    int pad = cols - 4 - (int)msg.size();
    for (int i = 0; i < pad; ++i) waddch(mainWin_, ' ');
    wattroff(mainWin_, COLOR_PAIR(pair));
    wrefresh(mainWin_);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Menu widget — returns selected index or -1 (Esc/q)
// ─────────────────────────────────────────────────────────────────────────────
int App::drawMenu(WINDOW* w, const std::vector<std::string>& items,
                  int selected, int startRow, bool numbered) {
    int cols = getmaxx(w);
    for (int i = 0; i < (int)items.size(); ++i) {
        std::string line;
        if (numbered)
            line = " [" + std::to_string(i + 1) + "] " + items[i];
        else
            line = "  " + items[i];
        // Pad
        while ((int)line.size() < cols - 4) line += ' ';
        if ((int)line.size() > cols - 4) line = line.substr(0, cols - 4);

        if (i == selected) {
            wattron(w, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
            mvwprintw(w, startRow + i, 2, "%s", line.c_str());
            wattroff(w, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        } else {
            wattron(w, COLOR_PAIR(CP_NORMAL));
            mvwprintw(w, startRow + i, 2, "%s", line.c_str());
            wattroff(w, COLOR_PAIR(CP_NORMAL));
        }
    }
    return selected;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inline text input box — returns false if user cancelled
// ─────────────────────────────────────────────────────────────────────────────
bool App::inputBox(WINDOW* w, int row, int col, int width,
                   const std::string& label, std::string& value,
                   const std::string& defaultVal) {
    wattron(w, COLOR_PAIR(CP_NORMAL) | A_BOLD);
    mvwprintw(w, row, col, "%s", label.c_str());
    wattroff(w, COLOR_PAIR(CP_NORMAL) | A_BOLD);

    int inputCol = col + (int)label.size() + 1;
    int inputWidth = width - (int)label.size() - 1;
    if (inputWidth < 4) inputWidth = 4;

    // Draw box
    wattron(w, COLOR_PAIR(CP_BOX));
    mvwprintw(w, row, inputCol - 1, "[");
    for (int i = 0; i < inputWidth; ++i) mvwaddch(w, row, inputCol + i, ' ');
    mvwprintw(w, row, inputCol + inputWidth, "]");
    wattroff(w, COLOR_PAIR(CP_BOX));

    if (value.empty() && !defaultVal.empty()) {
        // Show placeholder
        wattron(w, COLOR_PAIR(CP_DIM));
        mvwprintw(w, row, inputCol, "%s", defaultVal.substr(0, inputWidth).c_str());
        wattroff(w, COLOR_PAIR(CP_DIM));
    }

    wrefresh(w);

    // Collect input
    curs_set(1);
    std::string buf = value;
    int cursor = (int)buf.size();

    auto redraw = [&]() {
        // Clear field
        for (int i = 0; i < inputWidth; ++i)
            mvwaddch(w, row, inputCol + i, ' ');
        int dispStart = std::max(0, cursor - inputWidth + 1);
        wattron(w, COLOR_PAIR(CP_NORMAL));
        mvwprintw(w, row, inputCol,
                  "%s", buf.substr(dispStart, inputWidth).c_str());
        wattroff(w, COLOR_PAIR(CP_NORMAL));
        wmove(w, row, inputCol + std::min(cursor - dispStart, inputWidth - 1));
        wrefresh(w);
    };

    redraw();

    bool accepted = false;
    int ch;
    while ((ch = wgetch(w)) != ERR) {
        if (ch == '\n' || ch == KEY_ENTER) {
            if (buf.empty() && !defaultVal.empty()) buf = defaultVal;
            value = buf;
            accepted = true;
            break;
        } else if (ch == 27) {   // ESC
            break;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (cursor > 0) { buf.erase(--cursor, 1); redraw(); }
        } else if (ch == KEY_DC) {
            if (cursor < (int)buf.size()) { buf.erase(cursor, 1); redraw(); }
        } else if (ch == KEY_LEFT) {
            if (cursor > 0) { --cursor; redraw(); }
        } else if (ch == KEY_RIGHT) {
            if (cursor < (int)buf.size()) { ++cursor; redraw(); }
        } else if (ch == KEY_HOME) {
            cursor = 0; redraw();
        } else if (ch == KEY_END) {
            cursor = (int)buf.size(); redraw();
        } else if (isprint(ch) && (int)buf.size() < inputWidth * 2) {
            buf.insert(cursor++, 1, (char)ch);
            redraw();
        }
    }
    curs_set(0);
    return accepted;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Slider widget — returns false on Esc
// ─────────────────────────────────────────────────────────────────────────────
bool App::slider(WINDOW* w, int row, const std::string& label,
                 int& value, int minV, int maxV, int step) {
    int cols = getmaxx(w);
    int barWidth = cols - (int)label.size() - 16;
    if (barWidth < 10) barWidth = 10;

    auto redraw = [&]() {
        wattron(w, COLOR_PAIR(CP_NORMAL) | A_BOLD);
        mvwprintw(w, row, 2, "%s", label.c_str());
        wattroff(w, COLOR_PAIR(CP_NORMAL) | A_BOLD);

        int filled = (int)((float)(value - minV) / (maxV - minV) * barWidth);
        wattron(w, COLOR_PAIR(CP_SUCCESS));
        mvwprintw(w, row, 2 + (int)label.size() + 1, "[");
        for (int i = 0; i < filled; ++i)       mvwaddch(w, row, 3 + (int)label.size() + 1 + i, '=');
        wattron(w, COLOR_PAIR(CP_DIM));
        for (int i = filled; i < barWidth; ++i) mvwaddch(w, row, 3 + (int)label.size() + 1 + i, '-');
        wattron(w, COLOR_PAIR(CP_SUCCESS));
        mvwprintw(w, row, 3 + (int)label.size() + 1 + barWidth, "] %3d", value);
        wattroff(w, COLOR_PAIR(CP_SUCCESS));
        wrefresh(w);
    };

    redraw();
    int ch;
    while ((ch = wgetch(w)) != ERR) {
        if (ch == KEY_LEFT  || ch == 'h') { value = std::max(minV, value - step); redraw(); }
        else if (ch == KEY_RIGHT || ch == 'l') { value = std::min(maxV, value + step); redraw(); }
        else if (ch == '\n') return true;
        else if (ch == 27)   return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Confirm dialog
// ─────────────────────────────────────────────────────────────────────────────
bool App::confirmDialog(const std::string& question) {
    int dw = std::min(50, COLS_ - 4);
    int dh = 7;
    int dy = (ROWS_ - dh) / 2;
    int dx = (COLS_ - dw) / 2;
    WINDOW* dw_ = newwin(dh, dw, dy, dx);
    keypad(dw_, TRUE);
    drawBorder(dw_, "Confirm");
    centerText(dw_, 2, question, CP_WARNING, A_BOLD);

    int selected = 1; // default: No
    auto redrawBtns = [&]() {
        wattron(dw_, selected == 0 ? COLOR_PAIR(CP_BTN_SEL) : COLOR_PAIR(CP_BTN));
        mvwprintw(dw_, 4, dw / 2 - 8, "  YES  ");
        wattroff(dw_, selected == 0 ? COLOR_PAIR(CP_BTN_SEL) : COLOR_PAIR(CP_BTN));
        wattron(dw_, selected == 1 ? COLOR_PAIR(CP_BTN_SEL) : COLOR_PAIR(CP_BTN));
        mvwprintw(dw_, 4, dw / 2 + 1, "   NO  ");
        wattroff(dw_, selected == 1 ? COLOR_PAIR(CP_BTN_SEL) : COLOR_PAIR(CP_BTN));
        wrefresh(dw_);
    };

    redrawBtns();
    bool result = false;
    int ch;
    while ((ch = wgetch(dw_)) != ERR) {
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t') {
            selected = 1 - selected;
            redrawBtns();
        } else if (ch == 'y' || ch == 'Y') { result = true; break; }
        else if (ch == 'n' || ch == 'N' || ch == 27) { result = false; break; }
        else if (ch == '\n') { result = (selected == 0); break; }
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                // Simple mouse: left half = YES, right half = NO
                if (ev.bstate & BUTTON1_CLICKED) {
                    if (ev.x < dx + dw / 2) selected = 0;
                    else selected = 1;
                    redrawBtns();
                } else if (ev.bstate & BUTTON1_DOUBLE_CLICKED) {
                    if (ev.x < dx + dw / 2) selected = 0;
                    else selected = 1;
                    result = (selected == 0);
                    break;
                }
            }
        }
    }
    delwin(dw_);
    touchwin(mainWin_);
    wrefresh(mainWin_);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main run loop
// ─────────────────────────────────────────────────────────────────────────────
int App::run() {
    current_ = Screen::DepCheck;
    while (current_ != Screen::Quit) {
        werase(mainWin_);
        drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
        drawHeader(mainWin_, 1);

        switch (current_) {
            case Screen::DepCheck:    current_ = screenDepCheck();    break;
            case Screen::Resolution:  current_ = screenResolution();  break;
            case Screen::Framerate:   current_ = screenFramerate();   break;
            case Screen::Audio:       current_ = screenAudio();       break;
            case Screen::Encoder:     current_ = screenEncoder();     break;
            case Screen::Output:      current_ = screenOutput();      break;
            case Screen::Confirm:     current_ = screenConfirm();     break;
            case Screen::Recording:   current_ = screenRecording();   break;
            case Screen::Help:        current_ = screenHelp(Screen::Resolution); break;
            case Screen::Result:      /* handled inline */             break;
            case Screen::Quit:        break;
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Dependency check
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenDepCheck() {
    deps_ = checkDependencies();
    werase(mainWin_);
    drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
    drawHeader(mainWin_, 1);

    centerText(mainWin_, 4, "Checking system dependencies...", CP_TITLE, A_BOLD);
    wrefresh(mainWin_);

    int row = 6;
    bool allOk = true;
    bool ffmpegOk = false;
    for (auto& d : deps_) {
        std::string mark  = d.present ? "✓" : "✗";
        int         pair  = d.present ? CP_SUCCESS : CP_ERROR;
        wattron(mainWin_, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(mainWin_, row, 4, "%s  %-14s", mark.c_str(), d.name.c_str());
        wattroff(mainWin_, COLOR_PAIR(pair) | A_BOLD);
        if (!d.present) {
            wattron(mainWin_, COLOR_PAIR(CP_DIM));
            mvwprintw(mainWin_, row, 22, "Install: %s", d.installHint.c_str());
            wattroff(mainWin_, COLOR_PAIR(CP_DIM));
            if (d.name == "ffmpeg") ffmpegOk = false;
            allOk = false;
        } else {
            if (d.name == "ffmpeg") ffmpegOk = true;
        }
        row++;
    }

    // Disk space
    uint64_t freeMB = freeDiskMB();
    bool diskOk = freeMB >= 500;
    {
        std::string mark = diskOk ? "✓" : "✗";
        int pair = diskOk ? CP_SUCCESS : CP_WARNING;
        wattron(mainWin_, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(mainWin_, row, 4, "%s  Free disk:     %lu MB%s",
                  mark.c_str(), (unsigned long)freeMB,
                  diskOk ? "" : "  (< 500 MB — recording may fail!)");
        wattroff(mainWin_, COLOR_PAIR(pair) | A_BOLD);
        row++;
    }

    // DISPLAY env
    bool dispOk = (getenv("DISPLAY") != nullptr);
    {
        std::string mark = dispOk ? "✓" : "✗";
        int pair = dispOk ? CP_SUCCESS : CP_ERROR;
        wattron(mainWin_, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(mainWin_, row, 4, "%s  DISPLAY:       %s",
                  mark.c_str(), dispOk ? cfg_.displayStr.c_str() : "Not set!");
        wattroff(mainWin_, COLOR_PAIR(pair) | A_BOLD);
        if (!dispOk) allOk = false;
        row++;
    }

    row += 1;

    if (!ffmpegOk) {
        centerText(mainWin_, row++, "ERROR: ffmpeg is required to record!", CP_ERROR, A_BOLD);
        centerText(mainWin_, row++, "sudo apt install ffmpeg", CP_WARNING, 0);
        drawFooter(mainWin_, ROWS_ - 2, "[q] Quit");
        wrefresh(mainWin_);
        int ch;
        while ((ch = wgetch(mainWin_)) != 'q' && ch != 'Q' && ch != 27);
        return Screen::Quit;
    }

    if (allOk) {
        wattron(mainWin_, COLOR_PAIR(CP_SUCCESS) | A_BOLD);
        mvwprintw(mainWin_, row, 4, "All critical dependencies satisfied.");
        wattroff(mainWin_, COLOR_PAIR(CP_SUCCESS) | A_BOLD);
    } else {
        wattron(mainWin_, COLOR_PAIR(CP_WARNING) | A_BOLD);
        mvwprintw(mainWin_, row, 4, "Some optional dependencies are missing (xdpyinfo).");
        wattroff(mainWin_, COLOR_PAIR(CP_WARNING) | A_BOLD);
    }

    row += 2;
    centerText(mainWin_, row, "Press ENTER to continue or 'q' to quit", CP_DIM, 0);
    drawFooter(mainWin_, ROWS_ - 2, "[Enter] Continue   [q] Quit   [?] Help");
    wrefresh(mainWin_);

    int ch;
    while (true) {
        ch = wgetch(mainWin_);
        if (ch == '\n' || ch == KEY_ENTER) return Screen::Resolution;
        if (ch == 'q'  || ch == 'Q' || ch == 27) return Screen::Quit;
        if (ch == '?'  || ch == 'h') return screenHelp(Screen::Resolution);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Generic option-screen driver
// ─────────────────────────────────────────────────────────────────────────────
static Screen runOptionScreen(WINDOW* w, int ROWS_, int COLS_,
    const std::string& title, const std::string& subtitle,
    const std::vector<std::string>& items,
    int& selected, Screen prev, Screen next,
    const std::string& footerHints,
    std::function<void()> extraDraw = nullptr)
{
    (void)COLS_;
    int startRow = 6;

    while (true) {
        werase(w);
        // Reuse: header drawn outside; here draw title + items

        wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
        mvwprintw(w, 3, 3, "%s", title.c_str());
        wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);

        if (!subtitle.empty()) {
            wattron(w, COLOR_PAIR(CP_DIM));
            mvwprintw(w, 4, 3, "%s", subtitle.c_str());
            wattroff(w, COLOR_PAIR(CP_DIM));
            startRow = 6;
        }

        int cols = getmaxx(w);
        for (int i = 0; i < (int)items.size(); ++i) {
            std::string line = " [" + std::to_string(i + 1) + "] " + items[i];
            while ((int)line.size() < cols - 6) line += ' ';
            if ((int)line.size() > cols - 6) line = line.substr(0, cols - 6);
            if (i == selected) {
                wattron(w, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
                mvwprintw(w, startRow + i, 3, "%s", line.c_str());
                wattroff(w, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
            } else {
                wattron(w, COLOR_PAIR(CP_NORMAL));
                mvwprintw(w, startRow + i, 3, "%s", line.c_str());
                wattroff(w, COLOR_PAIR(CP_NORMAL));
            }
        }

        if (extraDraw) extraDraw();

        // Footer
        wattron(w, COLOR_PAIR(CP_DIM));
        mvwhline(w, ROWS_ - 3, 1, ACS_HLINE, cols - 2);
        mvwprintw(w, ROWS_ - 2, 2, "%s", footerHints.c_str());
        wattroff(w, COLOR_PAIR(CP_DIM));

        wrefresh(w);

        int ch = wgetch(w);
        if (ch == KEY_UP   || ch == 'k') { if (selected > 0) --selected; }
        else if (ch == KEY_DOWN || ch == 'j') { if (selected < (int)items.size()-1) ++selected; }
        else if (ch >= '1' && ch <= '9') {
            int idx = ch - '1';
            if (idx < (int)items.size()) { selected = idx; return next; }
        }
        else if (ch == '\n' || ch == KEY_ENTER) return next;
        else if (ch == KEY_BACKSPACE || ch == KEY_LEFT || ch == 'b') return prev;
        else if (ch == 27 || ch == 'q') return Screen::Quit;
        else if (ch == '?') return Screen::Help;
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & BUTTON1_CLICKED)) {
                int idx = ev.y - startRow;
                if (idx >= 0 && idx < (int)items.size()) {
                    selected = idx;
                    return next;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Resolution
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenResolution() {
    std::vector<std::string> items = {
        "720p   HD       (1280×720)   — Smaller file, faster encode",
        "1080p  Full HD  (1920×1080)  — Standard quality",
        "1440p  2K       (2560×1440)  — High clarity",
        "4K     UHD      (3840×2160)  — Maximum quality (large files)",
        "Native          (" + nativeRes_ + ")  — Match your screen exactly",
    };
    int sel = (int)cfg_.resolution;
    werase(mainWin_);
    drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
    drawHeader(mainWin_, 1);

    Screen result = runOptionScreen(mainWin_, ROWS_, COLS_,
        "Step 1 / 5  —  Resolution",
        "Select output video resolution:  ↑/↓ or J/K to navigate, Enter or click to select",
        items, sel, Screen::DepCheck, Screen::Framerate,
        "[↑↓/jk] Navigate   [Enter/Click] Select   [b] Back   [?] Help   [q] Quit");
    cfg_.resolution = (ResolutionMode)sel;
    if (result == Screen::Help) return screenHelp(Screen::Resolution);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Framerate
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenFramerate() {
    std::vector<std::string> items = {
        "24 FPS  — Cinematic look, smallest file",
        "30 FPS  — Smooth for most content",
        "60 FPS  — Fluid gameplay & screencasts",
    };
    int sel = (int)cfg_.framerate;
    werase(mainWin_);
    drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
    drawHeader(mainWin_, 1);

    Screen result = runOptionScreen(mainWin_, ROWS_, COLS_,
        "Step 2 / 5  —  Frame Rate",
        "Select recording frame rate:",
        items, sel, Screen::Resolution, Screen::Audio,
        "[↑↓/jk] Navigate   [Enter/Click] Select   [b] Back   [?] Help   [q] Quit");
    cfg_.framerate = (FramerateMode)sel;
    if (result == Screen::Help) return screenHelp(Screen::Framerate);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Audio
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenAudio() {
    auto sinks = listPulseSinks();
    std::vector<std::string> items = {
        "No Audio          — Video only, silent recording",
        "System Audio      — Capture computer playback (" + cfg_.audioDevice + ")",
    };
    // Add individual sinks
    for (size_t i = 0; i < sinks.size() && i < 4; ++i)
        items.push_back("Device: " + sinks[i]);

    int sel = (cfg_.audio == AudioMode::None) ? 0 : 1;
    werase(mainWin_);
    drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
    drawHeader(mainWin_, 1);

    Screen result = runOptionScreen(mainWin_, ROWS_, COLS_,
        "Step 3 / 5  —  Audio Source",
        "Choose audio capture mode:",
        items, sel, Screen::Framerate, Screen::Encoder,
        "[↑↓/jk] Navigate   [Enter/Click] Select   [b] Back   [?] Help   [q] Quit");

    if (sel == 0) {
        cfg_.audio = AudioMode::None;
    } else if (sel == 1) {
        cfg_.audio = AudioMode::System;
        cfg_.audioDevice = getSystemAudioMonitor();
    } else {
        cfg_.audio = AudioMode::CustomDevice;
        cfg_.audioDevice = sinks[sel - 2];
    }

    if (result == Screen::Help) return screenHelp(Screen::Audio);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Encoder settings
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenEncoder() {
    std::vector<std::string> presetItems = {
        "Ultrafast  — Lowest CPU, largest file, zero lag",
        "Fast       — Good balance for most systems",
        "Medium     — Better compression, moderate CPU",
        "Slow       — Best compression, high CPU",
    };
    int psel = (int)cfg_.preset;

    while (true) {
        werase(mainWin_);
        drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
        drawHeader(mainWin_, 1);

        wattron(mainWin_, COLOR_PAIR(CP_TITLE) | A_BOLD);
        mvwprintw(mainWin_, 3, 3, "Step 4 / 5  —  Encoder Settings");
        wattroff(mainWin_, COLOR_PAIR(CP_TITLE) | A_BOLD);
        wattron(mainWin_, COLOR_PAIR(CP_DIM));
        mvwprintw(mainWin_, 4, 3, "Choose encoder preset and quality (CRF):");
        wattroff(mainWin_, COLOR_PAIR(CP_DIM));

        // Preset menu
        int cols = getmaxx(mainWin_);
        for (int i = 0; i < (int)presetItems.size(); ++i) {
            std::string line = " [" + std::to_string(i + 1) + "] " + presetItems[i];
            while ((int)line.size() < cols - 6) line += ' ';
            if (i == psel) {
                wattron(mainWin_, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
                mvwprintw(mainWin_, 6 + i, 3, "%s", line.substr(0, cols-6).c_str());
                wattroff(mainWin_, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
            } else {
                wattron(mainWin_, COLOR_PAIR(CP_NORMAL));
                mvwprintw(mainWin_, 6 + i, 3, "%s", line.substr(0, cols-6).c_str());
                wattroff(mainWin_, COLOR_PAIR(CP_NORMAL));
            }
        }

        // CRF slider row
        wattron(mainWin_, COLOR_PAIR(CP_DIM));
        mvwprintw(mainWin_, 11, 3, "CRF (quality): 0=lossless, 23=default, 51=worst");
        wattroff(mainWin_, COLOR_PAIR(CP_DIM));

        // Show cursor state toggle
        wattron(mainWin_, COLOR_PAIR(CP_NORMAL));
        mvwprintw(mainWin_, 16, 3, "Show cursor in recording:  ");
        wattroff(mainWin_, COLOR_PAIR(CP_NORMAL));
        wattron(mainWin_, COLOR_PAIR(cfg_.showCursor ? CP_SUCCESS : CP_DIM) | A_BOLD);
        mvwprintw(mainWin_, 16, 30, cfg_.showCursor ? "[ON ] (press C to toggle)" : "[OFF] (press C to toggle)");
        wattroff(mainWin_, COLOR_PAIR(cfg_.showCursor ? CP_SUCCESS : CP_DIM) | A_BOLD);

        // CRF display
        int crfBarW = cols - 30;
        if (crfBarW < 10) crfBarW = 10;
        int filled = (int)((float)cfg_.crf / 51.0f * crfBarW);
        wattron(mainWin_, COLOR_PAIR(CP_SUCCESS));
        mvwprintw(mainWin_, 13, 3, "CRF %-2d  [", cfg_.crf);
        wattroff(mainWin_, COLOR_PAIR(CP_SUCCESS));
        wattron(mainWin_, COLOR_PAIR(CP_SUCCESS));
        for (int i = 0; i < filled; ++i)     mvwaddch(mainWin_, 13, 12 + i, '=');
        wattron(mainWin_, COLOR_PAIR(CP_DIM));
        for (int i = filled; i < crfBarW; ++i) mvwaddch(mainWin_, 13, 12 + i, '-');
        wattron(mainWin_, COLOR_PAIR(CP_SUCCESS));
        mvwprintw(mainWin_, 13, 12 + crfBarW, "]");
        wattroff(mainWin_, COLOR_PAIR(CP_SUCCESS));
        mvwprintw(mainWin_, 14, 3, " ← smaller CRF = higher quality, bigger file →");

        drawFooter(mainWin_, ROWS_ - 2,
            "[↑↓] Preset  [-/+] CRF  [C] Toggle cursor  [Enter] Next  [b] Back  [q] Quit");
        wrefresh(mainWin_);

        int ch = wgetch(mainWin_);
        if (ch == KEY_UP   || ch == 'k') { if (psel > 0) --psel; }
        else if (ch == KEY_DOWN || ch == 'j') { if (psel < 3) ++psel; }
        else if (ch >= '1' && ch <= '4') psel = ch - '1';
        else if (ch == '-' || ch == KEY_LEFT) cfg_.crf = std::max(0, cfg_.crf - 1);
        else if (ch == '+' || ch == '=' || ch == KEY_RIGHT) cfg_.crf = std::min(51, cfg_.crf + 1);
        else if (ch == 'c' || ch == 'C') cfg_.showCursor = !cfg_.showCursor;
        else if (ch == '\n' || ch == KEY_ENTER) {
            cfg_.preset = (EncoderPreset)psel;
            return Screen::Output;
        }
        else if (ch == 'b' || ch == KEY_BACKSPACE) { cfg_.preset = (EncoderPreset)psel; return Screen::Audio; }
        else if (ch == 27 || ch == 'q') return Screen::Quit;
        else if (ch == '?') return screenHelp(Screen::Encoder);
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & BUTTON1_CLICKED)) {
                int idx = ev.y - 6;
                if (idx >= 0 && idx < 4) psel = idx;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Output file
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenOutput() {
    std::string fname = cfg_.outputPath;
    // Strip .mp4 for display
    if (fname.size() > 4 && fname.substr(fname.size()-4) == ".mp4")
        fname = fname.substr(0, fname.size()-4);

    std::string errMsg;

    while (true) {
        werase(mainWin_);
        drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
        drawHeader(mainWin_, 1);

        wattron(mainWin_, COLOR_PAIR(CP_TITLE) | A_BOLD);
        mvwprintw(mainWin_, 3, 3, "Step 5 / 5  —  Output File");
        wattroff(mainWin_, COLOR_PAIR(CP_TITLE) | A_BOLD);
        wattron(mainWin_, COLOR_PAIR(CP_DIM));
        mvwprintw(mainWin_, 4, 3, "Enter filename (without .mp4 extension). Paths allowed: /home/you/rec");
        wattroff(mainWin_, COLOR_PAIR(CP_DIM));

        // Show free disk
        uint64_t fmb = freeDiskMB();
        int pair = fmb >= 1000 ? CP_SUCCESS : (fmb >= 300 ? CP_WARNING : CP_ERROR);
        wattron(mainWin_, COLOR_PAIR(pair));
        mvwprintw(mainWin_, 6, 3, "Free disk space: %lu MB", (unsigned long)fmb);
        wattroff(mainWin_, COLOR_PAIR(pair));

        // Input box
        if (inputBox(mainWin_, 8, 3, COLS_ - 8, "Filename: ", fname, "recording")) {
            std::string validated = validateOutputPath(fname.empty() ? "recording" : fname, errMsg);
            if (!errMsg.empty()) {
                wattron(mainWin_, COLOR_PAIR(CP_ERROR) | A_BOLD);
                mvwprintw(mainWin_, 10, 3, "Error: %s", errMsg.c_str());
                wattroff(mainWin_, COLOR_PAIR(CP_ERROR) | A_BOLD);
                wrefresh(mainWin_);
                napms(1800);
                continue;
            }

            // Warn if file exists
            if (fs::exists(validated)) {
                if (!confirmDialog("File already exists! Overwrite?")) {
                    werase(mainWin_);
                    drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
                    drawHeader(mainWin_, 1);
                    continue;
                }
            }
            cfg_.outputPath = validated;
            LOG_INFO("Output path set to: " + cfg_.outputPath);
            return Screen::Confirm;
        } else {
            return Screen::Encoder; // Esc = go back
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Confirm / summary
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenConfirm() {
    while (true) {
        werase(mainWin_);
        drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
        drawHeader(mainWin_, 1);

        centerText(mainWin_, 3, "─── Recording Summary ───", CP_TITLE, A_BOLD);

        int r = 5;
        auto row = [&](const std::string& label, const std::string& val, int valPair = CP_HIGHLIGHT) {
            wattron(mainWin_, COLOR_PAIR(CP_NORMAL) | A_BOLD);
            mvwprintw(mainWin_, r, 4, "%-18s", label.c_str());
            wattroff(mainWin_, COLOR_PAIR(CP_NORMAL) | A_BOLD);
            wattron(mainWin_, COLOR_PAIR(valPair));
            mvwprintw(mainWin_, r, 23, "%s", val.c_str());
            wattroff(mainWin_, COLOR_PAIR(valPair));
            r++;
        };

        row("Resolution:",  resolutionStr(cfg_));
        row("Frame rate:",  framerateStr(cfg_.framerate) + " FPS");
        row("Audio:",       cfg_.audio == AudioMode::None ? "Disabled" :
                            "System Monitor (" + cfg_.audioDevice + ")", CP_SUCCESS);
        row("Encoder:",     "libx264 / " + presetStr(cfg_.preset));
        row("Quality CRF:", std::to_string(cfg_.crf) + "  (lower = better)");
        row("Show cursor:", cfg_.showCursor ? "Yes" : "No");
        row("Output file:", cfg_.outputPath, CP_WARNING);
        row("Display:",     cfg_.displayStr);

        r++;
        std::string cmd = buildFFmpegCommand(cfg_);
        wattron(mainWin_, COLOR_PAIR(CP_DIM));
        mvwprintw(mainWin_, r++, 4, "Command preview:");
        // Word-wrap the command
        int cols = getmaxx(mainWin_);
        int w = cols - 8;
        for (int off = 0; off < (int)cmd.size() && r < ROWS_ - 5; off += w) {
            mvwprintw(mainWin_, r++, 6, "%s", cmd.substr(off, w).c_str());
        }
        wattroff(mainWin_, COLOR_PAIR(CP_DIM));

        r++;
        wattron(mainWin_, COLOR_PAIR(CP_WARNING) | A_BOLD);
        mvwprintw(mainWin_, r, 4, "Press 'q' in the terminal to stop recording gracefully.");
        wattroff(mainWin_, COLOR_PAIR(CP_WARNING) | A_BOLD);

        drawFooter(mainWin_, ROWS_ - 2,
            "[Enter/R] Start Recording   [b] Back to Output   [e] Edit from start   [q] Quit");
        wrefresh(mainWin_);

        int ch = wgetch(mainWin_);
        if (ch == '\n' || ch == 'r' || ch == 'R') return Screen::Recording;
        else if (ch == 'b' || ch == KEY_BACKSPACE) return Screen::Output;
        else if (ch == 'e' || ch == 'E') return Screen::Resolution;
        else if (ch == '?' ) return screenHelp(Screen::Confirm);
        else if (ch == 27  || ch == 'q') return Screen::Quit;
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & BUTTON1_CLICKED)) {
                // Simple: clicking bottom half starts recording
                if (ev.y > ROWS_ / 2) return Screen::Recording;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Recording (live)
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenRecording() {
    // Suspend ncurses, run ffmpeg in the terminal
    def_prog_mode();
    endwin();

    std::string cmd = buildFFmpegCommand(cfg_);
    LOG_INFO("Starting recording: " + cmd);

    // Print instructions before handing over to ffmpeg
    printf("\n");
    printf("\033[1;41m  ● REC  \033[0m  \033[1;37mLinux Screen Recorder v%s\033[0m\n", VERSION);
    printf("\033[1;30m──────────────────────────────────────────────────────────\033[0m\n");
    printf("  Recording to: \033[1;36m%s\033[0m\n", cfg_.outputPath.c_str());
    printf("  Resolution:   \033[1;36m%s\033[0m   FPS: \033[1;36m%s\033[0m\n",
           resolutionStr(cfg_).c_str(), framerateStr(cfg_.framerate).c_str());
    printf("  Audio:        \033[1;36m%s\033[0m\n",
           cfg_.audio == AudioMode::None ? "Disabled" : cfg_.audioDevice.c_str());
    printf("\033[1;30m──────────────────────────────────────────────────────────\033[0m\n");
    printf("  \033[1;32mPress 'q'\033[0m in this window to stop recording gracefully.\n");
    printf("  \033[1;31mAvoid Ctrl+C\033[0m — it may corrupt the video file!\n");
    printf("\033[1;30m──────────────────────────────────────────────────────────\033[0m\n\n");
    fflush(stdout);

    int exitCode = std::system(cmd.c_str());
    LOG_INFO("ffmpeg exited with code: " + std::to_string(exitCode));

    // Restore ncurses
    reset_prog_mode();
    refresh();

    bool ok = (exitCode == 0 || exitCode == 255 * 256 || WEXITSTATUS(exitCode) == 255);
    // 255 = user quit with 'q'
    if (WIFEXITED(exitCode)) {
        int e = WEXITSTATUS(exitCode);
        ok = (e == 0 || e == 255);
    }

    return screenResult(ok, exitCode);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Result
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenResult(bool ok, int exitCode) {
    while (true) {
        werase(mainWin_);
        drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
        drawHeader(mainWin_, 1);

        if (ok) {
            centerText(mainWin_, 4, "✓  Recording Completed", CP_SUCCESS, A_BOLD);
            wattron(mainWin_, COLOR_PAIR(CP_NORMAL));
            mvwprintw(mainWin_, 6, 4, "Saved to:  %s", cfg_.outputPath.c_str());
            wattroff(mainWin_, COLOR_PAIR(CP_NORMAL));
            wattron(mainWin_, COLOR_PAIR(CP_DIM));
            mvwprintw(mainWin_, 8,  4, "Play with:  mpv \"%s\"", cfg_.outputPath.c_str());
            mvwprintw(mainWin_, 9,  4, "Or:         vlc \"%s\"", cfg_.outputPath.c_str());
            mvwprintw(mainWin_, 10, 4, "Or:         ffplay \"%s\"", cfg_.outputPath.c_str());
            wattroff(mainWin_, COLOR_PAIR(CP_DIM));

            // File size if available
            try {
                if (fs::exists(cfg_.outputPath)) {
                    auto sz = fs::file_size(cfg_.outputPath);
                    double mb = sz / 1024.0 / 1024.0;
                    wattron(mainWin_, COLOR_PAIR(CP_SUCCESS));
                    mvwprintw(mainWin_, 12, 4, "File size:  %.2f MB", mb);
                    wattroff(mainWin_, COLOR_PAIR(CP_SUCCESS));
                }
            } catch (...) {}
        } else {
            centerText(mainWin_, 4, "✗  Recording Failed", CP_ERROR, A_BOLD);
            wattron(mainWin_, COLOR_PAIR(CP_ERROR));
            mvwprintw(mainWin_, 6, 4, "Exit code: %d", exitCode);
            wattroff(mainWin_, COLOR_PAIR(CP_ERROR));

            wattron(mainWin_, COLOR_PAIR(CP_NORMAL));
            mvwprintw(mainWin_, 8,  4, "Possible causes:");
            mvwprintw(mainWin_, 9,  4, "  • Display server (X11) not accessible");
            mvwprintw(mainWin_, 10, 4, "  • PulseAudio monitor device not available");
            mvwprintw(mainWin_, 11, 4, "  • Insufficient disk space");
            mvwprintw(mainWin_, 12, 4, "  • Output path not writable");
            mvwprintw(mainWin_, 13, 4, "  • libx264 not available in your ffmpeg build");
            wattroff(mainWin_, COLOR_PAIR(CP_NORMAL));

            wattron(mainWin_, COLOR_PAIR(CP_DIM));
            const char* home = getenv("HOME");
            std::string logPath = home ? std::string(home) + "/.screen_recorder.log" : "/tmp/screen_recorder.log";
            mvwprintw(mainWin_, 15, 4, "See log: %s", logPath.c_str());
            wattroff(mainWin_, COLOR_PAIR(CP_DIM));
        }

        drawFooter(mainWin_, ROWS_ - 2,
            "[r] Record Again   [e] Edit Settings   [q] Quit");
        wrefresh(mainWin_);

        int ch = wgetch(mainWin_);
        if (ch == 'r' || ch == 'R') return Screen::Recording;
        if (ch == 'e' || ch == 'E') return Screen::Resolution;
        if (ch == 'q' || ch == 'Q' || ch == 27) return Screen::Quit;
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & BUTTON1_CLICKED))
                return Screen::Quit;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Screen: Help
// ─────────────────────────────────────────────────────────────────────────────
Screen App::screenHelp(Screen returnTo) {
    werase(mainWin_);
    drawBorder(mainWin_, std::string(APP_NAME) + " v" + VERSION);
    drawHeader(mainWin_, 1);

    centerText(mainWin_, 3, "Help & Keyboard Reference", CP_TITLE, A_BOLD);

    struct Row { std::string key; std::string desc; };
    std::vector<Row> rows = {
        {"↑ / ↓ / j / k",    "Navigate menu items"},
        {"Enter / Click",     "Select item / confirm"},
        {"b / Backspace",     "Go back one screen"},
        {"- / +  (CRF)",      "Decrease / increase quality value"},
        {"← / → (CRF/Slider)","Adjust slider value"},
        {"C  (Encoder page)", "Toggle cursor visibility"},
        {"r",                 "Record again (on Result screen)"},
        {"e",                 "Edit settings from beginning"},
        {"q / Q / Esc",       "Quit application"},
        {"?  / h",            "Show this help screen"},
        {"",                  ""},
        {"In ffmpeg window:",  ""},
        {"q",                 "Stop recording gracefully (recommended)"},
        {"Ctrl+C",            "Emergency stop (may corrupt video!)"},
    };

    int r = 5;
    for (auto& row : rows) {
        if (row.key.empty()) { r++; continue; }
        bool isHeader = row.desc.empty();
        wattron(mainWin_, COLOR_PAIR(isHeader ? CP_TITLE : CP_WARNING) | (isHeader ? A_BOLD : 0));
        mvwprintw(mainWin_, r, 4, "%-28s", row.key.c_str());
        wattroff(mainWin_, COLOR_PAIR(isHeader ? CP_TITLE : CP_WARNING) | A_BOLD);
        if (!isHeader) {
            wattron(mainWin_, COLOR_PAIR(CP_NORMAL));
            mvwprintw(mainWin_, r, 33, "%s", row.desc.c_str());
            wattroff(mainWin_, COLOR_PAIR(CP_NORMAL));
        }
        r++;
    }

    drawFooter(mainWin_, ROWS_ - 2, "[Any key] Return");
    wrefresh(mainWin_);
    wgetch(mainWin_);
    return returnTo;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // Simple --version flag
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--version" || std::string(argv[i]) == "-v") {
            printf("%s v%s\n", APP_NAME, VERSION);
            return 0;
        }
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printf("Usage: %s [--version] [--help]\n", argv[0]);
            printf("  Interactive TUI screen recorder powered by ffmpeg.\n");
            printf("  Run without arguments to launch the interface.\n");
            return 0;
        }
    }

    try {
        App app;
        return app.run();
    } catch (const std::exception& ex) {
        // ncurses might be in a bad state; try to clean up
        endwin();
        fprintf(stderr, "\033[1;31mFatal error: %s\033[0m\n", ex.what());
        return 2;
    } catch (...) {
        endwin();
        fprintf(stderr, "\033[1;31mFatal: unknown exception\033[0m\n");
        return 2;
    }
}
