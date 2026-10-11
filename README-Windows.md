# ColorSlicer for Windows

[← Back to the main README](README.md)

ColorSlicer is a fork of PrusaSlicer 3.0.0-alpha12 that keeps colors and print settings from foreign 3MF files
and colors from OBJ files. **Not affiliated with Prusa Research.** AGPL-3.0.

There are three ways to get it on Windows 10/11 (64-bit). Pick one.

| | Needs on your PC | Time |
|---|---|---|
| **A. Download the .zip** | nothing | 1 minute |
| **B. Let GitHub build it for you** (fork or this repo) | only a browser | 3-4 h the first time, then about 1 h |
| **C. Build it yourself from a clean PC** | Visual Studio, CMake, Git, 40 GB free | 3-6 h the first time |

---

## A. Download and run

1. Go to the [Releases page](https://github.com/PrintsNCode/ColorSlicer/releases) and download
   `ColorSlicer-windows-x64-<version>.zip` (when a Windows build is attached to the release).
2. Right click the .zip → **Extract all…** into a normal folder (for example `C:\ColorSlicer`). Do not run it from inside the zip.
3. Double click `ColorSlicer.exe`.

First launch:

- **"Windows protected your PC" (SmartScreen).** The app is not code-signed. Click **More info → Run anyway**.
- **Missing `VCRUNTIME140.dll` / `MSVCP140.dll`.** Install the
  [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).
- **Blank panels or web views.** Install the
  [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) (already part of Windows 11).
- To keep it apart from an existing PrusaSlicer, start it with a separate data folder:
  `ColorSlicer.exe --datadir C:\ColorSlicer-data`.

---

## B. Build it on GitHub (no tools on your PC)

The workflow [`.github/workflows/colorslicer_windows.yml`](.github/workflows/colorslicer_windows.yml) builds on a
GitHub Windows runner (free for public repositories).

1. Open the repository → **Actions** → **ColorSlicer Windows** → **Run workflow** (branch `colorslicer`).
2. Wait. The first run compiles all dependencies (about 2-3 h); the result is cached, so later runs are shorter.
3. When it turns green, open the run → **Artifacts** → `ColorSlicer-windows-x64` and download the zip.
4. To publish it: push a tag such as `v3.0.0-alpha12-colorslicer.2`; the workflow attaches the zip to that Release.

If the first run fails, open the failing step and read the last lines of its log; that is where to look.

---

## C. Build it yourself on a clean PC

Requirements: Windows 10/11 64-bit, 16 GB RAM (8 GB can work, slowly), **40 GB free disk**, internet.

### 1. Install the tools

Open **PowerShell** as administrator and run (or install them by hand from the websites):

```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e --version 3.27.9
winget install --id Microsoft.VisualStudio.2022.Community -e --override "--passive --wait --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
```

By hand instead of the third line: download *Visual Studio 2022 Community*, and in the installer tick
**Desktop development with C++** (it includes the MSVC compiler and the Windows SDK).

Use **CMake 3.27.x**. CMake 4.x gave problems with this tree on macOS and is untested here.
Close PowerShell and open it again so `git` and `cmake` are on the PATH, then check:

```powershell
git --version
cmake --version
```

### 2. Get the source

Use a **short path**: some dependencies fail with Windows' 260-character path limit.

```powershell
git config --global core.longpaths true
git clone --branch colorslicer https://github.com/PrintsNCode/ColorSlicer.git C:\cs
cd C:\cs
```

### 3. Build the dependencies (the long step)

Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu, then:

```cmd
cd C:\cs
cmake -S deps -B deps\build -G "Visual Studio 17 2022" -A x64 -DDEP_DEBUG=OFF
cmake --build deps\build --config Release --parallel
```

This takes 2-4 hours. `-DDEP_DEBUG=OFF` skips the debug versions, which halves the time. When it ends, run the
last command again: it must finish in seconds without compiling anything.

### 4. Build ColorSlicer

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSLIC3R_BUILD_TESTS=OFF -DSLIC3R_ENABLE_WIN10_MESH_REPAIR=OFF -DCMAKE_PREFIX_PATH="%CD%/deps/build/destdir/usr/local"
cmake --build build --config Release --parallel
```

Add `-DSLIC3R_BUILD_TESTS=ON` and run
`build\src\slic3r-shared\Release\slic3r-shared-tests.exe "[objcolors],[colors],[colorimport],[foreignprint]"`
if you want to run the color tests.

### 5. Run it

```cmd
build\src\slic3r-app-launcher\Release\slic3r-app-launcher.exe --datadir C:\ColorSlicer-data
```

The `resources` folder is linked next to the executable by the build, so it runs from there.

### 6. Make a portable folder / zip

```powershell
$src = "C:\cs\build\src\slic3r-app-launcher\Release"
New-Item -ItemType Directory -Force C:\cs\dist\ColorSlicer | Out-Null
Get-ChildItem $src -File | Where-Object { $_.Extension -in '.exe','.dll' } | Copy-Item -Destination C:\cs\dist\ColorSlicer
Rename-Item C:\cs\dist\ColorSlicer\slic3r-app-launcher.exe ColorSlicer.exe
Copy-Item -Recurse -Force C:\cs\resources C:\cs\dist\ColorSlicer\resources
Copy-Item C:\cs\LICENSE C:\cs\dist\ColorSlicer
Compress-Archive -Path C:\cs\dist\ColorSlicer\* -DestinationPath C:\cs\dist\ColorSlicer-windows-x64.zip
```

If the output folder differs on your machine, search for it: `dir C:\cs\build -Recurse -Filter slic3r-app-launcher.exe`.

### Common problems

| Symptom | Fix |
|---|---|
| `cmake` is not recognized | Close and reopen the terminal; check the PATH |
| Path too long / file not found while extracting | Clone into `C:\cs` and run `git config --global core.longpaths true` |
| Boost extraction fails with a UTF-8 error | Use the "x64 Native Tools" prompt and make sure the path has no accents or spaces |
| Out of memory while compiling | Use `--parallel 2` instead of `--parallel` |
| Dependencies fail halfway | Run the same build command again; it continues where it stopped |

> Status: the Windows build files were written but **not yet run on a real Windows machine** when this page was
> written. If a step fails, open an issue with the last lines of the log.

---

## What ColorSlicer does

See the [main README](README.md): colors and print settings from Bambu / Orca / Anycubic / MakerWorld / Blender 3MF
files, and colors from OBJ files, opened in PrusaSlicer 3.

*PrusaSlicer is a trademark of Prusa Research a.s. ColorSlicer is an independent project licensed under AGPL-3.0.*
