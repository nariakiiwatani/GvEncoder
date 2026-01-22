# GvEncoder

GvEncoder is a command-line and GUI tool that generates `.gv` files for `ofxExtremeGpuVideo`.
It supports image sequence folders and video files (via `ffmpeg`), and includes a GUI player for `.gv`.

## Features
- CLI and GUI workflows
- Input: image sequence folder or video file
- Options: codec, fps, resize, output path, delete source
- GUI playback of `.gv` (auto-load after encode)
- Multiple inputs (queue)

## Requirements
- macOS
- CMake 3.20+
- `ffmpeg` (for video input)

## Getting Started
This repo uses `ofxExtremeGpuVideo` as a submodule.

```
git submodule update --init --recursive
```

## Build
Generate `CMakePresets.json` with TrussC Project Generator (it contains your local TrussC path).

- macOS
```
cmake --preset macos
cmake --build --preset macos
```

- Windows
```
cmake --preset windows
cmake --build --preset windows
```

- Linux
```
cmake --preset linux
cmake --build --preset linux
```

- (Manual)
```
cmake -S . -B build
cmake --build build
```

### Run (CLI)
```
GvEncoder [options] <input>
```

Examples:
```
GvEncoder -c dxt5 -r 30 -s 1920x1080 -o /path/to/output.gv /path/to/video.mov
GvEncoder /path/to/sequence_folder
```

### Run (GUI)
Launch the app without arguments.

## Codecs
- **DXT1**: RGB focused, no alpha or 1-bit alpha, smallest size
- **DXT3**: 4-bit explicit alpha, sharp edges and UI-friendly
- **DXT5**: interpolated alpha, best for smooth transparency

## Acknowledgements
GvEncoder is built on top of **TrussC** and **ofxExtremeGpuVideo**.
Huge respect to the authors and maintainers of both projects for making this possible.

- **TrussC**: the creative coding framework that powers the app runtime and GUI.  
  Official site: https://trussc.org
- **ofxExtremeGpuVideo** (by Ushio): the `.gv` format, encoder/reader logic, and GPU-friendly workflow.  
  Repository: https://github.com/Ushio/ofxExtremeGpuVideo

## Submodules
- `ofxExtremeGpuVideo` (zlib-style license)
  - includes `lz4` and `squish` in its `libs/` folder

## License
This project is licensed under the MIT License.  
See `LICENSE` for details.

