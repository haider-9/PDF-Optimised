# CanvasPDF

A native desktop PDF viewer built with Qt 6 and MuPDF.

## Download a Release

Download the latest Linux AppImage from the
[CanvasPDF releases page](https://github.com/haider-9/pdf-optimised/releases).
Make it executable and launch it:

```bash
chmod +x ~/Downloads/CanvasPDF-linux-x86_64.AppImage
~/Downloads/CanvasPDF-linux-x86_64.AppImage
```

No build tools are needed to run the AppImage.

## Build on Linux

On Arch Linux, install the build dependencies:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base libmupdf
```

From the project directory, configure and build CanvasPDF:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the built application with:

```bash
./build/CanvasPDF
```

## Add to the Application Menu

Install CanvasPDF in a stable location so it can be found from the desktop
launcher. Use the command for the version you want:

```bash
# Downloaded AppImage
install -Dm755 ~/Downloads/CanvasPDF-linux-x86_64.AppImage \
  "$HOME/.local/bin/CanvasPDF"

# Or, built from source
install -Dm755 build/CanvasPDF "$HOME/.local/bin/CanvasPDF"
```

Install the icon and desktop launcher from the project directory:

```bash
install -Dm644 packaging/canvaspdf.png \
  "$HOME/.local/share/icons/hicolor/256x256/apps/canvaspdf.png"
mkdir -p "$HOME/.local/share/applications"
sed "s|^Exec=CanvasPDF %f|Exec=\"$HOME/.local/bin/CanvasPDF\" %f|" \
  packaging/CanvasPDF.desktop \
  > "$HOME/.local/share/applications/canvaspdf.desktop"
```

CanvasPDF should now appear in your application menu and be searchable by
name. It is also registered as a PDF viewer. If it does not appear right
away, log out and back in so your desktop refreshes its application list.