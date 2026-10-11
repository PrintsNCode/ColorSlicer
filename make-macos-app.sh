#!/bin/bash
# Packs a macOS build of PrusaSlicer 3 into a .app bundle and a .dmg image.
# Usage: ./make-macos-app.sh <repo folder> [AppName]
# Needs an existing build in <repo>/build (cmake --build build).
set -euo pipefail

REPO="$(cd "${1:?usage: $0 <repo folder> [AppName]}" && pwd)"
APP_NAME="${2:-ColorSlicer}"
BUILD="$REPO/build"
LAUNCHER="$BUILD/src/slic3r-app-launcher/slic3r-app-launcher"
PLIST_SRC="$BUILD/src/Info.plist"
OUT="$REPO/dist"
APP="$OUT/$APP_NAME.app"
DMG="$OUT/$APP_NAME.dmg"

[ -x "$LAUNCHER" ]   || { echo "Cannot find $LAUNCHER (build first with make)"; exit 1; }
[ -f "$PLIST_SRC" ]  || { echo "Cannot find $PLIST_SRC"; exit 1; }

echo "==> Creating $APP"
rm -rf "$APP" "$DMG"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"

# Info.plist: the executable must be named as CFBundleExecutable says.
EXE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$PLIST_SRC")"
cp "$PLIST_SRC" "$APP/Contents/Info.plist"
cp "$LAUNCHER" "$APP/Contents/MacOS/$EXE_NAME"
chmod +x "$APP/Contents/MacOS/$EXE_NAME"

# Resources (profiles, icons, shaders, translations...). -L in case the build links them with symlinks.
cp -RL "$REPO/resources/." "$APP/Contents/Resources/"
rm -rf "$APP/Contents/Resources/test_data"
cp "$REPO"/resources/icons/*.icns "$APP/Contents/Resources/" 2>/dev/null || true

# Non-system dynamic libraries are copied to Frameworks and their paths rewritten.
echo "==> Copying dynamic libraries"
is_system() { case "$1" in /usr/lib/*|/System/*) return 0;; *) return 1;; esac; }

bundle_deps() {
  local target="$1"
  otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
    is_system "$dep" && continue
    case "$dep" in
      @executable_path/*|@loader_path/*) continue ;;
      @rpath/*) local name="${dep#@rpath/}"; local src="" ;;
      *)        local name="$(basename "$dep")"; local src="$dep" ;;
    esac
    local dest="$APP/Contents/Frameworks/$(basename "$name")"
    if [ ! -f "$dest" ]; then
      if [ -z "$src" ] || [ ! -f "$src" ]; then
        src="$(find "$BUILD" "$REPO/deps" /opt/homebrew/lib /usr/local/lib -name "$(basename "$name")" -type f 2>/dev/null | head -n1 || true)"
      fi
      [ -n "$src" ] && [ -f "$src" ] || { echo "WARNING: cannot find $dep"; continue; }
      cp "$src" "$dest"; chmod u+w "$dest"
      install_name_tool -id "@executable_path/../Frameworks/$(basename "$dest")" "$dest"
      bundle_deps "$dest"
    fi
    install_name_tool -change "$dep" "@executable_path/../Frameworks/$(basename "$dest")" "$target" 2>/dev/null || true
  done
}
bundle_deps "$APP/Contents/MacOS/$EXE_NAME"

echo "==> Signing (ad-hoc, no developer account)"
find "$APP/Contents/Frameworks" -type f \( -name "*.dylib" -o -name "*.so" \) -exec codesign --force -s - {} \; 2>/dev/null || true
codesign --force --deep -s - "$APP"

echo "==> Creating $DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

echo
echo "Done:"
echo "  $APP"
echo "  $DMG"
