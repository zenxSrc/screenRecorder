# Linux Screen Recorder

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
