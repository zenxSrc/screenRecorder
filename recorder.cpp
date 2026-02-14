#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <array>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <csignal>

// Global flag for signal handling
volatile sig_atomic_t interrupted = 0;

void signalHandler(int signum) {
    interrupted = 1;
}

// --- Helper: Run Shell Command ---
std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        std::cerr << "Warning: Failed to execute command: " << cmd << std::endl;
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// --- GUI Helpers ---
void clearScreen() {
    std::cout << "\033[2J\033[1;1H" << std::flush;
}

void printBox(const std::string& content, const std::string& color = "\033[1;36m") {
    std::cout << color << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << content;
    // Pad to 54 chars
    int padding = 54 - content.length();
    for (int i = 0; i < padding; i++) std::cout << " ";
    std::cout << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\033[0m\n";
}

void printHeader(const std::string& title) {
    clearScreen();
    std::cout << "\033[1;36m";
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                        ║\n";
    std::cout << "║           🎬  LINUX SCREEN RECORDER  🎬               ║\n";
    std::cout << "║                                                        ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    std::cout << "\033[0m\n";
    std::cout << "\033[1;33m┌─ " << title << "\033[0m\n";
}

void printSeparator() {
    std::cout << "\033[1;30m────────────────────────────────────────────────────────\033[0m\n";
}

std::string getInput(const std::string& prompt) {
    std::cout << "\n\033[1;37m" << prompt << "\033[0m\n";
    std::cout << "\033[1;33m▶\033[0m ";
    std::string input;
    std::getline(std::cin, input);

    // Trim whitespace
    input.erase(0, input.find_first_not_of(" \t\n\r"));
    input.erase(input.find_last_not_of(" \t\n\r") + 1);

    return input;
}

int getValidChoice(const std::string& prompt, int min, int max) {
    while (true) {
        std::string input = getInput(prompt);
        try {
            int choice = std::stoi(input);
            if (choice >= min && choice <= max) {
                return choice;
            }
            std::cout << "\033[1;31m✗ Invalid choice. Please enter " << min << "-" << max << ".\033[0m\n";
        } catch (...) {
            std::cout << "\033[1;31m✗ Invalid input. Please enter a number.\033[0m\n";
        }
    }
}

// --- Detect screen resolution ---
std::string detectScreenResolution() {
    // Try xdpyinfo first (X11)
    std::string detectRes = exec("xdpyinfo 2>/dev/null | awk '/dimensions:/ {print $2}'");
    if (!detectRes.empty()) {
        detectRes.erase(std::remove(detectRes.begin(), detectRes.end(), '\n'), detectRes.end());
        if (!detectRes.empty() && detectRes.find('x') != std::string::npos) {
            return detectRes;
        }
    }

    // Try xrandr as fallback
    detectRes = exec("xrandr 2>/dev/null | grep '*' | awk '{print $1}' | head -n1");
    if (!detectRes.empty()) {
        detectRes.erase(std::remove(detectRes.begin(), detectRes.end(), '\n'), detectRes.end());
        if (!detectRes.empty() && detectRes.find('x') != std::string::npos) {
            return detectRes;
        }
    }

    // Default fallback
    return "1920x1080";
}

// --- Get system audio monitor device ---
std::string getSystemAudioDevice() {
    // Get the default sink's monitor (this captures system playback audio)
    std::string sinkName = exec("pactl get-default-sink 2>/dev/null");
    if (!sinkName.empty()) {
        sinkName.erase(std::remove(sinkName.begin(), sinkName.end(), '\n'), sinkName.end());
        return sinkName + ".monitor";
    }

    // Fallback: try to find any monitor device
    std::string monitors = exec("pactl list sources short 2>/dev/null | grep monitor | head -n1 | awk '{print $2}'");
    if (!monitors.empty()) {
        monitors.erase(std::remove(monitors.begin(), monitors.end(), '\n'), monitors.end());
        return monitors;
    }

    // Last resort
    return "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor";
}

// --- Check if ffmpeg is available ---
bool checkFFmpeg() {
    std::string result = exec("which ffmpeg 2>/dev/null");
    return !result.empty();
}

int main() {
    // Set up signal handler for Ctrl+C
    signal(SIGINT, signalHandler);

    // --- Check prerequisites ---
    if (!checkFFmpeg()) {
        std::cerr << "\n\033[1;31m✗ Error: ffmpeg is not installed!\033[0m\n";
        std::cerr << "  Install with: \033[1;33msudo apt install ffmpeg\033[0m\n\n";
        return 1;
    }

    // --- 1. Resolution Selection ---
    printHeader("VIDEO QUALITY");
    std::cout << "\n\033[1;37mChoose recording resolution:\033[0m\n\n";
    std::cout << "  \033[1;32m[1]\033[0m 720p  HD      (1280x720  - Smaller file size)\n";
    std::cout << "  \033[1;32m[2]\033[0m 1080p Full HD (1920x1080 - Standard quality)\n";
    std::cout << "  \033[1;32m[3]\033[0m Native        (Original screen resolution)\n";

    int resChoice = getValidChoice("Enter your choice [1-3]:", 1, 3);

    // Auto-detect screen resolution
    std::string screenRes = detectScreenResolution();
    std::cout << "\n\033[1;36mℹ Detected: " << screenRes << "\033[0m\n";

    std::string scaleFilter = "";
    std::string resolutionDesc;
    if (resChoice == 1) {
        scaleFilter = " -vf scale=1280:720";
        resolutionDesc = "720p (1280x720)";
    } else if (resChoice == 2) {
        scaleFilter = " -vf scale=1920:1080";
        resolutionDesc = "1080p (1920x1080)";
    } else {
        resolutionDesc = "Native (" + screenRes + ")";
    }

    // --- 2. Audio Selection ---
    printHeader("AUDIO SETTINGS");
    std::cout << "\n\033[1;37mSelect audio source:\033[0m\n\n";
    std::cout << "  \033[1;32m[0]\033[0m No Audio       (Video only)\n";
    std::cout << "  \033[1;32m[1]\033[0m System Audio   (Capture computer playback: videos, games, music)\n";
    std::cout << "\n\033[1;30m  Note: System audio captures what your computer is playing,\n";
    std::cout << "        not microphone input.\033[0m\n";

    int audioChoice = getValidChoice("Enter your choice [0-1]:", 0, 1);
    bool recordAudio = (audioChoice == 1);

    std::string audioDevice;
    if (recordAudio) {
        audioDevice = getSystemAudioDevice();
        std::cout << "\n\033[1;36mℹ Audio device: " << audioDevice << "\033[0m\n";
    }

    // --- 3. Filename ---
    printHeader("OUTPUT FILE");
    std::cout << "\n\033[1;37mEnter filename for your recording:\033[0m\n";
    std::cout << "\033[1;30m(Leave empty for 'recording.mp4')\033[0m\n";

    std::string savePath = getInput("Filename:");
    if (savePath.empty()) {
        savePath = "recording";
    }

    // Ensure .mp4 extension
    if (savePath.find(".mp4") == std::string::npos) {
        savePath += ".mp4";
    }

    std::cout << "\n\033[1;32m✓ Saving to: " << savePath << "\033[0m\n";

    // --- 4. Build Command ---
    std::stringstream cmd;
    const char* display = std::getenv("DISPLAY");
    std::string displayStr = display ? display : ":0";

    // Video Input
    cmd << "ffmpeg -f x11grab -framerate 60"
        << " -thread_queue_size 4096"
        << " -video_size " << screenRes
        << " -i " << displayStr;

    // Audio Input (use monitor device for system audio)
    if (recordAudio) {
        cmd << " -f pulse -thread_queue_size 2048 -i " << audioDevice;
    }

    // Filters & Encoding
    if (!scaleFilter.empty()) {
        cmd << scaleFilter;
    }

    cmd << " -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -r 60";

    if (recordAudio) {
        cmd << " -c:a aac -b:a 192k -ac 2";
    }

    cmd << " -y \"" << savePath << "\" 2>&1";

    // --- 5. Display final settings ---
    printHeader("READY TO RECORD");
    printSeparator();
    std::cout << "\n\033[1;37m📹 RECORDING SETTINGS:\033[0m\n\n";
    std::cout << "  Display:    \033[1;36m" << displayStr << "\033[0m\n";
    std::cout << "  Resolution: \033[1;36m" << resolutionDesc << "\033[0m\n";
    std::cout << "  Framerate:  \033[1;36m60 FPS\033[0m\n";
    std::cout << "  Audio:      \033[1;36m" << (recordAudio ? "System Audio (Monitor)" : "Disabled") << "\033[0m\n";
    std::cout << "  Output:     \033[1;36m" << savePath << "\033[0m\n";
    std::cout << "\n";
    printSeparator();

    std::cout << "\n\033[1;33m⚠️  CONTROLS:\033[0m\n\n";
    std::cout << "  \033[1;32m'q'\033[0m         → Stop recording gracefully (recommended)\n";
    std::cout << "  \033[1;31mCtrl+C\033[0m     → Emergency stop (may corrupt video)\n";
    std::cout << "\n";
    printSeparator();

    std::cout << "\n\033[1;37mPress ENTER to start recording...\033[0m";
    std::cin.get();

    // --- 6. Start Recording ---
    clearScreen();
    std::cout << "\n";
    printBox("🔴 RECORDING IN PROGRESS", "\033[1;41m");
    std::cout << "\n";
    std::cout << "\033[1;37m  Recording to: \033[1;36m" << savePath << "\033[0m\n\n";
    std::cout << "  \033[1;32mPress 'q' in the ffmpeg window to stop\033[0m\n";
    std::cout << "  \033[1;31m(Avoid Ctrl+C - it may corrupt the video!)\033[0m\n\n";
    printSeparator();
    std::cout << "\n";

    int ret = std::system(cmd.str().c_str());

    // --- 7. Show result ---
    clearScreen();
    std::cout << "\n";

    if (interrupted) {
        std::cout << "\033[1;33m";
        printBox("⚠️  RECORDING INTERRUPTED", "\033[1;33m");
        std::cout << "\n\033[1;33m  Recording was stopped with Ctrl+C.\033[0m\n";
        std::cout << "  \033[1;33mThe video file may be incomplete or corrupted.\033[0m\n";
        std::cout << "  \033[1;37mTry playing: \033[1;36m" << savePath << "\033[0m\n\n";
    } else if (ret == 0 || ret == 255) {  // 255 often means user quit with 'q'
        std::cout << "\033[1;32m";
        printBox("✓ RECORDING COMPLETED", "\033[1;42m");
        std::cout << "\n\033[1;32m  Successfully saved to: \033[1;37m" << savePath << "\033[0m\n\n";
        std::cout << "  \033[1;37mPlay with: \033[1;36mmpv " << savePath << "\033[0m\n";
        std::cout << "  \033[1;37mOr:        \033[1;36mvlc " << savePath << "\033[0m\n\n";
    } else {
        std::cout << "\033[1;31m";
        printBox("✗ RECORDING FAILED", "\033[1;41m");
        std::cout << "\n\033[1;31m  Error code: " << ret << "\033[0m\n";
        std::cout << "  \033[1;37mPossible causes:\033[0m\n";
        std::cout << "    • Display server not accessible\n";
        std::cout << "    • Audio device not available\n";
        std::cout << "    • Insufficient disk space\n";
        std::cout << "    • Permission denied\n\n";
        return 1;
    }

    return 0;
}
