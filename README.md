# FastImageViewer

A lightweight, modular, and extensible C++ image viewer for fast RAW image culling and comparison. Compatible with Windows and Linux.

## Features
- Fast RAW image loading (libraw)
- Cross-platform display (Qt)
- Modular and configurable design
- Lightweight and extensible

## Dependencies
- CMake >= 3.10
- Qt5 or Qt6 (Qt6 preferred)
- libraw
- OpenGL (for future extensions)

## Build Instructions

### Windows
1. Install [CMake](https://cmake.org/download/)
2. Install Qt and libraw (use vcpkg, Qt installer, or download binaries)
3. Open a terminal in the project folder and run:
   ```
   mkdir build
   cd build
   cmake .. -G "Visual Studio 16 2019" # or your version
   cmake --build .
   ```

### Linux
1. Install dependencies:
   ```
   sudo apt-get install cmake qt6-base-dev libraw-dev
   ```
2. Build:
   ```
   mkdir build
   cd build
   cmake ..
   make
   ```

## Usage
- Run the executable and open a RAW image file.
- Keyboard shortcuts and configuration options will be added as the project evolves.

## Extending
- Add new modules in the `src/` folder and update `CMakeLists.txt`.
- Use Qt and libraw APIs for custom features.
