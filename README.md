
# CanvasPDF

A native desktop PDF viewer built with C++20, Qt 6 Widgets, and MuPDF.

CanvasPDF is designed for navigating large, high-resolution PDF documents
without relying on a browser runtime or uploading documents to a web service.

## Table of Contents

- Overview
- Features
- Technology Stack
- System Requirements
- Install Dependencies on Arch Linux
- Install Dependencies on Windows
- Verify Dependencies
- Build from Source
- Build and Run on Windows
- Run CanvasPDF
- Install Locally
- Keyboard and Mouse Controls
- Git and GitHub CLI
- Troubleshooting
- Current Limitations
- Licensing

## Overview

CanvasPDF is a native C++ desktop application using Qt for its graphical
interface and MuPDF for PDF rendering.

The project uses CMake as its build system and Ninja as its build backend.

The project supports Linux and Windows builds. Linux instructions use Arch
Linux; Windows instructions use the MSYS2 UCRT64 environment so Qt, MuPDF,
pkg-config, and the compiler come from one compatible toolchain.

## Features

- Native desktop application
- PDF rendering using MuPDF
- Mouse-wheel zoom
- Mouse-based panning
- Page navigation
- Fit-to-view and zoom controls
- Basic pencil annotation mode
- Background PDF rendering
- CMake-based build configuration

Feature availability may change as the application develops.

## Technology Stack

| Component | Purpose |
| --- | --- |
| C++20 | Application implementation |
| Qt 6 Widgets | Desktop interface |
| MuPDF | PDF document loading and rendering |
| CMake | Build configuration |
| Ninja | Build execution |
| GCC | C++ compilation |
| pkg-config | Dependency discovery |
| Git | Source control |
| GitHub CLI (`gh`) | Optional GitHub integration |

## System Requirements

For the Linux build, install:

- GCC and standard development tools
- CMake
- Ninja
- Qt 6 Base, including Widgets
- MuPDF development libraries and headers
- MuPDF command-line tools
- pkg-config

For Windows, use 64-bit Windows 10 or later with MSYS2 UCRT64, MinGW-w64,
CMake, Ninja, Qt 6 Widgets, MuPDF, and pkgconf. Build and launch the app from
the UCRT64 shell so its runtime DLLs are on `PATH`.

Git and GitHub CLI are optional for building the application but useful
for source control and publishing the project.

## Install Dependencies on Arch Linux

Update the system and install the packages required to build CanvasPDF:

```bash
sudo pacman -Syu
```

Install the compiler, build tools, Qt, MuPDF, and package discovery utility:

```bash
sudo pacman -S --needed \
  base-devel \
  gcc \
  cmake \
  ninja \
  pkgconf \
  qt6-base \
  libmupdf \
  mupdf-tools
```

Package purposes:

- `base-devel`: Standard development tools used for compiling software.
- `gcc`: C and C++ compiler toolchain.
- `cmake`: Configures the project and generates build files.
- `ninja`: Executes the generated build.
- `pkgconf`: Provides pkg-config-compatible dependency discovery.
- `qt6-base`: Qt 6 base libraries and Widgets.
- `libmupdf`: MuPDF libraries and development headers.
- `mupdf-tools`: MuPDF command-line utilities, useful for inspecting and
  testing PDF files.

### Install Git and GitHub CLI

Install Git and the GitHub CLI separately:

```bash
sudo pacman -S --needed git github-cli
```

- `git` manages the project's source history.
- `gh` provides command-line integration with GitHub.

Neither package is required to launch an already-built CanvasPDF executable.

### Optional X11 Compatibility

If you are running a Wayland desktop and need to launch Qt using its X11
backend, install XWayland if it is not already present:

```bash
sudo pacman -S --needed xorg-xwayland
```

The Qt XCB platform plugin must also be available through the installed
Qt packages.

## Install Dependencies on Windows

1. Install MSYS2 from [msys2.org](https://www.msys2.org/) using its default
   location, `C:\msys64`.
2. Open **MSYS2 UCRT64** from the Start menu. Do not use the plain MSYS, MINGW64,
   or CLANG64 terminal for this recipe.
3. Update the environment:

```bash
pacman -Syu
```

If the updater asks you to close the terminal, close it, reopen **MSYS2
UCRT64**, and run `pacman -Syu` again until no updates remain.
4. Install the compiler, build tools, Qt, MuPDF, and pkgconf:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-libmupdf \
  mingw-w64-ucrt-x86_64-pkgconf
```

These packages provide the MinGW-w64 C++ compiler, Qt 6 Widgets, MuPDF's
headers and libraries, and the `mupdf` pkg-config metadata used by CMake.

## Verify Dependencies

Check the installed compiler and build tools:

```bash
g++ --version
cmake --version
ninja --version
pkg-config --version
```

Check Qt 6 Widgets:

```bash
pkg-config --modversion Qt6Widgets
```

Check MuPDF:

```bash
pkg-config --modversion mupdf
```

Check that the MuPDF headers and shared library are present:

```bash
ls /usr/include/mupdf
ls /usr/lib/libmupdf.so
```

If a command fails, verify that the corresponding package is installed.

## Build from Source

### 1. Enter the project directory

```bash
cd ~/Projects/CanvasPDF
```

The project should contain at least:

```text
CanvasPDF/
├── CMakeLists.txt
├── README.md
├── src/
│   └── main.cpp
└── build/
```

The `build/` directory is generated during compilation and may not exist
before the first build.

### 2. Configure a Release build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

This command configures the project and discovers Qt and MuPDF.

### 3. Compile the application

```bash
cmake --build build -j2
```

The `-j2` option limits the build to two parallel jobs, which is a reasonable
starting point for systems with limited CPU resources.

Increase the job count if appropriate for your hardware.

### 4. Verify the executable

```bash
ls -lh build/CanvasPDF
```

The expected executable path is:

```text
build/CanvasPDF
```

## Run CanvasPDF

From the project directory:

```bash
./build/CanvasPDF
```

If Qt has display or input issues in a Wayland session, try the X11 backend:

```bash
QT_QPA_PLATFORM=xcb ./build/CanvasPDF
```

This changes the Qt platform backend for that launch only.

Use the application's file-opening control to select a PDF document.

## Build and Run on Windows

Open **MSYS2 UCRT64** and change to the project directory. For example, a
project under your Windows profile is available as
`/c/Users/<username>/Projects/CanvasPDF`:

```bash
cd /c/Users/<username>/Projects/CanvasPDF
```

Configure and build in a separate directory so a Linux `build/` directory is
not reused:

```bash
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
```

Launch the native Windows GUI executable from the same UCRT64 shell:

```bash
./build-windows/CanvasPDF.exe
```

To open a PDF from the command line, quote its path when it contains spaces:

```bash
./build-windows/CanvasPDF.exe "/c/Users/<username>/Documents/example.pdf"
```

The UCRT64 shell supplies Qt, MuPDF, and MinGW runtime DLL locations. The
executable is not a self-contained Windows installer; do not copy it alone to
another PC. A redistributable package must include the Qt platform plugin,
MuPDF and its runtime dependencies, and comply with MuPDF's licensing terms.

## Install CanvasPDF for Your User

After a successful build, install the application under `~/.local`:

```bash
cmake --install build --prefix "$HOME/.local"
```

The executable should be installed at:

```text
~/.local/bin/CanvasPDF
```

Launch it with:

```bash
~/.local/bin/CanvasPDF
```

If you want to launch the application with the X11 Qt backend:

```bash
QT_QPA_PLATFORM=xcb ~/.local/bin/CanvasPDF
```

This installation procedure installs the target defined in CMake. It does not
automatically create a desktop launcher or package every external runtime
dependency.

## Rebuild After Code Changes

After modifying the source code, rebuild with:

```bash
cd ~/Projects/CanvasPDF
cmake --build build -j2
```

If CMake configuration or dependency detection becomes inconsistent, perform
a clean configure and build:

```bash
cd ~/Projects/CanvasPDF
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

The clean rebuild removes generated build files. It does not remove the
source files in `src/`.

## Keyboard and Mouse Controls

| Input | Action |
| --- | --- |
| Mouse wheel | Zoom |
| Left mouse drag | Pan in navigation mode |
| Middle mouse drag | Pan |
| `F` | Next page |
| `D` | Previous page |
| `N` | Toggle pencil mode |
| `Escape` | Exit pencil mode |

Use the application's toolbar for page selection, fit-to-view, and zoom
controls where available.

Controls may be updated as the application evolves.

## Git and GitHub CLI

### 1. Check the installed versions

```bash
git --version
gh --version
```

### 2. Configure your Git identity

Replace the sample values with your own author details:

```bash
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

Verify the configuration:

```bash
git config --global --list
```

### 3. Initialize a repository

From the project directory:

```bash
cd ~/Projects/CanvasPDF
git init
```

If the directory is already a Git repository, do not initialize it again
unnecessarily.

### 4. Create a `.gitignore`

Before staging the source code, create a `.gitignore` file:

```bash
nano .gitignore
```

Add the following entries:

```gitignore
/build/
/build-*/
/CMakeFiles/
/CMakeCache.txt
/compile_commands.json
*.o
*.obj
*.so
*.dll
*.exe
*.log
```

Save and close the file.

### 5. Review and commit the project

Inspect the repository:

```bash
git status
```

Stage the project files:

```bash
git add CMakeLists.txt README.md .gitignore src/
```

Review what is staged:

```bash
git diff --cached
```

Create the first commit:

```bash
git commit -m "Initial CanvasPDF implementation"
```

### 6. Authenticate with GitHub

If you intend to use GitHub:

```bash
gh auth login
```

Follow the interactive prompts to authenticate.

Git works locally without a GitHub account or remote repository.

## Troubleshooting

### CMake cannot find Qt

Check that Qt 6 Base is installed:

```bash
sudo pacman -S --needed qt6-base
pkg-config --modversion Qt6Widgets
```

### CMake cannot find MuPDF

Check the MuPDF packages and metadata:

```bash
sudo pacman -S --needed libmupdf mupdf-tools pkgconf
pkg-config --modversion mupdf
pkg-config --cflags --libs mupdf
```

### The executable does not exist

Configure and compile the project again:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

### Qt fails to initialize its display backend

Try:

```bash
QT_QPA_PLATFORM=xcb ./build/CanvasPDF
```

If XCB is unavailable, check the Qt installation and XWayland availability.

### The application is slow with a large PDF

PDF rendering time and memory use depend on page dimensions, image content,
zoom level, and document complexity.

Start with a representative document and monitor memory use. Very large pages
may require substantial memory even when the PDF file itself is stored on disk.

## Current Limitations

- Rendering performance depends on document complexity and page dimensions.
- Very large PDFs should be tested with representative workloads.
- Pencil annotations may not persist after the application closes unless
  persistent annotation saving has been implemented.
- This README does not claim that full tiled rendering, unlimited document
  scalability, annotation persistence, or guaranteed memory bounds are
  implemented.
- Windows builds require a compatible C++ compiler, Qt 6, MuPDF, and their
  runtime dependencies. Windows packaging requires separate validation.

## Licensing

MuPDF is available under the GNU Affero General Public License (AGPL) or a
commercial license, subject to the applicable terms.

Review the license requirements before redistributing CanvasPDF. In
particular, assess the obligations that apply if distributing a modified
MuPDF-based application or a closed-source product.

This section is informational and is not legal advice.
# PDF-Optimised
