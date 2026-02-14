# 🎥 Linux Screen Recorder

A lightweight, dependency-free (except FFmpeg) command-line screen recorder for Linux (X11) with a simple interactive TUI menu.

## Features

- Records at 60 FPS using libx264 (CRF 23 – good quality/size balance)
- Choice of resolution: 720p, 1080p, or native screen resolution
- Optional system audio capture (via PulseAudio monitor source)
- Clean stop with 'q' key (recommended) to avoid video corruption
- Very low CPU usage thanks to ultrafast preset
- Single-file C++17 source – easy to compile and run
- Colorful terminal interface with clear instructions

## Requirements

- Linux with X11 (Wayland **not** supported)
- FFmpeg installed (`libx264` and `aac` encoders must be available)
- `xdpyinfo` or `xrandr` (usually pre-installed) for resolution detection
- PulseAudio (for system audio capture)

### Install FFmpeg (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install ffmpeg
```
## Installation

Clone the repository:
```
git clone https://github.com/zenxSrc/screenRecorder.git
cd screenRecorder
```
Compile the single source file:
```
g++ -std=c++17 -O2 recorder.cpp -o screenrecorder
```
(You can also use -Wall -Wextra for stricter warnings if desired)

## Usage
```
./screenrecorder
```
Follow the on-screen prompts:

- Choose video quality (720p / 1080p / Native)
- Choose whether to record system audio
- Enter output filename (defaults to recording.mp4)
- Press ENTER to begin recording
- Press q in the FFmpeg terminal window to stop gracefully

### Important: Avoid using Ctrl+C to stop — it may result in a corrupted/incomplete video file.

Example Output Filename
- If you leave the filename blank → saves as recording.mp4
- You can also specify full paths: ~/Videos/my_gameplay.mp4

## Known Limitations

- X11 only (no Wayland support yet)
- No microphone input (system audio only when enabled)
- No advanced encoding options (tuned for simplicity & performance)

## Building on Other Distros
Fedora:
```
sudo dnf install ffmpeg gcc-c++
```

Arch: 
```
sudo pacman -S ffmpeg gcc
```
## Contributing
Feel free to open issues or pull requests — especially welcome are Wayland support or additional features while keeping it minimal.

## License
MIT License (https://github.com/zenxSrc/screenRecorder/tree/main?tab=MIT-1-ov-file#)
