# Screen Recorder Documentation

## Overview
This high-performance C++ screen recorder allows users to capture their screen activities efficiently. It includes a command-line interface (CLI) and supports X11 systems, making it versatile for various Linux environments.

## Features
- **High Performance**: Optimized for minimal CPU usage and smooth frame rates.
- **Command-Line Interface (CLI)**: Simple and easy commands for quick setups and execution.
- **X11 Support**: Seamless integration with the X11 window system.
- **PulseAudio Integration**: Capture system audio along with screen recordings.
- **Configurable Resolutions**: Choose your preferred resolution for recordings.
- **System Audio Capture**: Record audio outputs from the system directly.
- **Graceful Error Handling**: Provides clear error messages and logging for troubleshooting.

## Usage Guide
1. **Installation**: 
   - Ensure you have a C++ compiler and CMake installed.
   - Clone the repository: `git clone <repository-url>`.
   - Navigate to the project directory: `cd <project-directory>`.
   - Build the project using CMake:
     ```bash
     mkdir build
     cd build
     cmake ..
     make
     ```
2. **Recording**:
   - Start recording with the command:
     ```bash
     ./screenRecorder -r <resolution> -o <output-file>
     ```
   - Example: `./screenRecorder -r 1920x1080 -o output.mp4`

## Technical Design
- **Architecture**: The application follows a modular design, executing audio and video processing in separate threads for optimal performance.
- **Libraries Used**:
  - OpenCV for video processing.
  - PulseAudio for audio handling.
  - Xlib for interaction with the X11 server.

## Build Instructions
- Follow the usage guide for installation and build steps.
- Make sure all dependencies are satisfied:
  - Install necessary packages: `sudo apt-get install libopencv-dev libpulse-dev libx11-dev`

## Troubleshooting
- **Common Issues**:
  - **Audio not captured**: Ensure PulseAudio is properly installed and running.
  - **Screen recording does not start**: Check if X11 is configured correctly.
  - **Permission denied**: Run the command with appropriate permissions or check file path where output is being saved.

For further assistance, check the GitHub issue tracker or contact support.