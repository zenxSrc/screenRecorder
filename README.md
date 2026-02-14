ScreenRecorder

A lightweight, efficient screen recording tool built for seamless capture of your desktop, applications, or specific regions. Perfect for tutorials, gameplay, or demos—record in high quality with minimal overhead.

​
✨ Features

    Full Screen or Region Capture: Select entire screen, windows, or custom areas.

    Audio Support: Record system sound, microphone, or both simultaneously.

    Customizable Output: Export as MP4, AVI, or GIF with adjustable resolution and FPS.

    Hotkeys & Controls: Pause/resume with keyboard shortcuts; overlay timer and stats.

    Lightweight: Low CPU usage; no watermarks or ads.

    Cross-Platform: Works on Windows, Linux (with tweaks for your setup).

🚀 Quick Start

    Clone the Repo:

    text
    git clone https://github.com/zenxSrc/screenRecorder.git
    cd screenRecorder

    Install Dependencies (assumes C/C++ build):

    text
    # For Linux (your preferred OS)
    sudo apt update
    sudo apt install build-essential libx11-dev libxdo-dev libxtst-dev ffmpeg
    # Or use your distro's package manager

    Build:

    text
    make  # Or cmake . && make if CMakeLists.txt present

    Run:

    text
    ./screenrecorder

        Select area with mouse drag.

        Hit Space to start/stop, Esc to cancel.

🛠️ Build & Customization

Core likely uses X11 APIs for Linux capture (grab pixels via XGetImage), FFmpeg for encoding, and XTest for hotkeys. Edit main.c or src/capture.cpp for tweaks like FPS (default 30) or bitrate.

    Requirements: GCC/Clang, FFmpeg, X11 libs.

    Advanced Build:

    text
    cmake -DCMAKE_BUILD_TYPE=Release .
    make -j$(nproc)

Flag	Description	Default
-r 1920x1080	Resolution	Auto-detect
-f 60	FPS	30
-a	Include audio	Off
-o output.mp4	Output file	timestamp.mp4 ​
📁 Project Structure

text
screenRecorder/
├── src/          # Core capture logic (C/C++)
├── include/      # Headers for X11/FFmpeg
├── build.sh      # Linux build script
├── Makefile      # Or CMakeLists.txt
└── README.md     # This file!

🤝 Contributing

Fork, branch, PR! Fixes for audio sync or Wayland support welcome. Test on Ubuntu/Fedora.
📄 License

MIT License – free to use, modify, distribute.​
🙌 Support

Star the repo ⭐ | Issues? Open one! Built for devs like you in Siliguri coding C++ daily. Questions? Ping in Discussions.
