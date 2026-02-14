Linux Screen Recorder is a simple, terminal-based tool that uses FFmpeg to capture your Linux screen (X11) with optional system audio. It offers an interactive menu for easy setup of resolution, audio, and output—no compilation needed, just FFmpeg installed.​
✨ Features

    Interactive TUI: Colorful console menu for resolution (720p/1080p/native), audio (system or none), and filename selection.

    High-Quality Capture: 60 FPS, libx264 encoding (CRF 23), optional scaling; system audio via PulseAudio monitor.

    Smart Detection: Auto-detects screen resolution (xdpyinfo/xrandr) and default audio sink.

    Graceful Controls: 'q' to stop FFmpeg safely; Ctrl+C handled but warned against (avoids corruption).

    Zero Dependencies Beyond FFmpeg: Pure C++ with standard libs + shell exec for one-liner recording.​

🚀 Quick Start

    Prerequisites (one command on Ubuntu/Debian):

    sudo apt update && sudo apt install ffmpeg pulseaudio-utils x11-utils xrandr

    PulseAudio for system audio monitor.​

    Compile & Run:

    text
    g++ -std=c++17 -O2 recorder.cpp -o screenrecorder
    chmod +x screenrecorder
    ./screenrecorder

    Usage Flow:

        Choose resolution (e.g., 1080p).

        Select audio (system playback like games/music).

        Enter filename (default: recording.mp4).

        Press Enter → Recording starts; press q in FFmpeg window to stop.​

Example output file: Crisp 1920x1080@60fps MP4, ready for VLC/mpv.
⚙️ Customization

Edit recorder.cpp for tweaks:

    Framerate: Change -framerate 60 and -r 60.

    Quality: Adjust -crf 23 (lower = better/smaller files).

    Audio: Modify getSystemAudioDevice() for mic or custom sinks.​

Option	Menu Choice	Command Snippet
720p	1	-vf scale=1280:720
1080p	2	-vf scale=1920:1080
Native	3	Auto via detectScreenResolution()
System Audio	1	-f pulse -i <sink>.monitor
🧪 Troubleshooting

    No audio? Ensure PulseAudio running; check pactl get-default-sink.

    Black screen? Run in X11 session (not Wayland); set DISPLAY=:0.

    FFmpeg missing? Script checks and prompts install.

    Ctrl+C corruption? Use 'q' instead—script warns!​

📝 Build Details

Single-file C++17 app (~11k chars). Uses:

    <iostream>, <string>, etc. for TUI (ANSI colors, input trimming).

    popen/exec for shell cmds (xdpyinfo, pactl).

    sig_atomic_t for SIGINT handling.
    No external libs—compiles anywhere with g++.​

🤝 Contributing

Add Wayland support? Mic input? PRs welcome! Test on your setup (Linux preferred).​

MIT – Fork and tweak freely. Star if useful! ⭐
