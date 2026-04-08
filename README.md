# 🎥 Linux Screen Recorder (Industrial TUI)

A powerful, **interactive terminal-based screen recorder** for Linux (X11), built in **C++17 + ncurses**, powered by **FFmpeg**.

> ⚡ Industrial-grade TUI • Keyboard + Mouse support • Safe & robust

---

## ✨ Features

### 🎬 Recording Engine

* FFmpeg-powered recording (`libx264 + AAC`)
* Real-time screen capture via **X11 (x11grab)**
* Automatic scaling (capture native → downscale if needed)
* Clean shutdown via `q` (prevents corruption)

---

### 🖥️ Advanced TUI (ncurses)

* Fully interactive **multi-step UI**
* **Keyboard + mouse support**
* Highlighted menus, dialogs, sliders, and input boxes
* Help screen with full keybindings
* Live status + structured workflow

---

### ⚙️ Recording Configuration

#### 📺 Resolution Options

* 720p / 1080p / 1440p / 4K
* Native screen resolution (auto-detected)

#### 🎞️ Frame Rate Control

* 24 FPS (cinematic)
* 30 FPS (balanced)
* 60 FPS (smooth gameplay)

#### 🔊 Audio System

* No audio mode
* Default system audio (PulseAudio monitor)
* **Custom audio device selection** (auto-detected sinks)

---

### 🎚️ Encoder & Quality Control

* Presets:

  * Ultrafast / Fast / Medium / Slow
* **CRF slider (0–51)** for fine quality control
* Toggle **mouse cursor visibility**
* Pixel format optimized for compatibility (`yuv420p`)

---

### 📁 Output Management

* Interactive filename input
* Auto `.mp4` handling
* Path validation:

  * Prevents invalid characters
  * Checks directory existence
  * Verifies write permissions
* Overwrite confirmation dialog

---

### 🔍 Smart System Detection

* Auto-detects:

  * Screen resolution (`xdpyinfo` / `xrandr`)
  * DISPLAY environment
  * PulseAudio monitor device
* Lists available audio sinks dynamically

---

### 🧪 Built-in Safety Checks

* Dependency checker (ffmpeg, pactl, xdpyinfo)
* Disk space validation (warns < 500MB)
* Terminal size validation
* Input sanitization (prevents unsafe filenames)

---

### 🧾 Logging System

* Persistent logs:

  ```
  ~/.screen_recorder.log
  ```
* Includes:

  * Command execution
  * Errors & warnings
  * Runtime events

---

### 🧵 Performance & Architecture

* Multi-threaded design
* Atomic state management
* Safe subprocess handling
* Efficient FFmpeg pipeline

---

## 📦 Requirements

* Linux (X11 required — ❌ Wayland not supported)
* FFmpeg (`libx264` + `aac`)
* PulseAudio (`pactl`)
* ncurses
* `xdpyinfo` or `xrandr`

---

## 🔧 Install Dependencies

### 🟢 Arch / Manjaro

```bash
sudo pacman -S ffmpeg ncurses libpulse
```

### 🔵 Ubuntu / Debian

```bash
sudo apt install ffmpeg libncurses-dev pulseaudio-utils x11-utils
```

---

## ⚙️ Build

```bash
g++ -std=c++17 -O2 -o screen_recorder screen_recorder.cpp \
-lncurses -lpanel -lmenu -lform -lpthread
```

---

## ▶️ Usage

```bash
./screen_recorder
```

---

## 🧭 Workflow

1. Dependency check
2. Select resolution
3. Select frame rate
4. Choose audio source
5. Configure encoder (preset + CRF + cursor)
6. Enter output filename
7. Review summary + FFmpeg command preview
8. Start recording

---

## 🎮 Controls

### General Navigation

* `↑ / ↓ / j / k` → Navigate
* `Enter / Click` → Select
* `b / Backspace` → Go back
* `q / Esc` → Quit
* `? / h` → Help screen

### Encoder Screen

* `- / +` → Adjust CRF
* `← / →` → Adjust slider
* `C` → Toggle cursor

### Recording

* `q` → Stop recording safely
* ⚠️ Avoid `Ctrl+C` (may corrupt file)

---

## 📤 Output

* Default: `recording.mp4`
* Supports full paths:

  ```
  ~/Videos/output.mp4
  ```

After recording:

* Shows file size
* Provides playback commands (mpv / vlc / ffplay)

---

## 🚫 Limitations

* Wayland not supported
* No microphone input (system audio only)
* No region/window capture (full screen only)

---

## 🚀 Future Improvements

* Wayland support
* Microphone input
* Region/window recording
* Hardware acceleration (VAAPI / NVENC)

---

## 📜 License

MIT License

---

## 👨‍💻 Author

Built with 💻 + ☕ by Dip 

