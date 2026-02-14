🎥 screenRecorder

screenRecorder is a lightweight, high-performance C++ CLI tool designed for capturing screen activity on Linux (X11) environments.

Engineered for efficiency, it utilizes multi-threading to handle video processing (OpenCV) and audio capture (PulseAudio) concurrently, ensuring minimal CPU overhead and smooth frame rates. It is ideal for headless servers, automated testing, or power users who prefer the terminal over a GUI.
⚡ Key Features

    🚀 High Performance: Optimized implementation ensures low latency and minimal resource consumption.

    🖥️ X11 Native: Deep integration with XLib for direct screen capture.

    🔊 Audio Sync: Robust PulseAudio integration for synchronized system audio recording.

    🔧 CLI First: specific command-line arguments for resolution, output paths, and formats.

    🧵 Multi-threaded Architecture: Decoupled audio and video threads prevent frame drops during high-load operations.

🛠️ Prerequisites

Before building, ensure your development environment has the necessary dependencies installed.

System Requirements:

    Linux (X11 Window System)

    C++ Compiler (C++17 or later recommended)

    CMake (3.10+)
