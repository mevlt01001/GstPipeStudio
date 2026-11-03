#!/usr/bin/env bash
# Builds, tests and packages GstPipeStudio as a .deb for the Ubuntu release it runs on (run as root).
#
#   docker run --rm -v <repo>:/repo:ro -v <Qt 6.8.3 gcc_64 or gcc_arm64>:/opt/Qt/current \
#              -v <outdir>:/dist ubuntu:22.04 /repo/gstpipestudio/packaging/build-deb.sh
#
# Environment: VERSION (default 0.1.0), QT_DIR (default /opt/Qt/current), OUT (default /dist), SKIP_TESTS=1.
# The package keeps Qt in its own folder, so it cannot clash with a Qt on the system; GStreamer always comes from the system.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(dirname "$(dirname "$here")")"
QT_DIR="${QT_DIR:-/opt/Qt/current}"
VERSION="${VERSION:-0.1.0}"
OUT="${OUT:-/dist}"
WORK="${WORK:-/tmp/gps-build}"

"$here/install-deps.sh"

# Work on a copy: the tests write next to their sources, and the repository may be mounted read-only.
rm -rf "$WORK" && mkdir -p "$WORK"
cp -a "$repo/gstpipestudio" "$WORK/src"
src="$WORK/src"

. /etc/os-release
ARCH="$(dpkg --print-architecture)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
NAME="gstpipestudio_${VERSION}_ubuntu${VERSION_ID}_${ARCH}.deb"

# ---- build and test
cmake -S "$src" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" \
      -DGPS_DIRS="core/plan;core/engine;tools;plugin;app;tests" -DGPS_TEST_DIRS="unit;engine;cli;plugin;gui"
cmake --build "$WORK/build"
if [ -z "${SKIP_TESTS:-}" ]; then
    (cd "$WORK/build" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure)
fi

# ---- stage the package tree
root="$WORK/pkg"
app=usr/lib/gstpipestudio
rm -rf "$root"
install -d "$root/$app/bin" "$root/$app/lib" "$root/$app/plugins" "$root/usr/bin" "$root/usr/lib/$MULTIARCH/gstreamer-1.0"
for tool in gstpipestudio gps-inspect gps-update gps-run; do
    install -m755 "$WORK/build/bin/$tool" "$root/$app/bin/$tool"
    ln -s "../lib/gstpipestudio/bin/$tool" "$root/usr/bin/$tool"
done
install -m644 "$WORK/build/lib/gstreamer-1.0/libgstgpsconf.so" "$root/usr/lib/$MULTIARCH/gstreamer-1.0/"
printf '[Paths]\nPrefix=..\nPlugins=plugins\nLibraries=lib\n' > "$root/$app/bin/qt.conf"

# Qt: the plugins the window needs (X11, Wayland, headless), then every Qt library the program and those plugins use.
for dir in platforminputcontexts platformthemes xcbglintegrations wayland-decoration-client \
           wayland-graphics-integration-client wayland-shell-integration; do
    [ -d "$QT_DIR/plugins/$dir" ] && cp -rL "$QT_DIR/plugins/$dir" "$root/$app/plugins/"
done
mkdir -p "$root/$app/plugins/platforms"
for plugin in libqxcb libqoffscreen libqwayland-egl libqwayland-generic; do
    [ -f "$QT_DIR/plugins/platforms/$plugin.so" ] && cp -L "$QT_DIR/plugins/platforms/$plugin.so" "$root/$app/plugins/platforms/"
done
(
    cd "$QT_DIR/plugins"
    for file in $(cd "$root/$app/plugins" && find . -name '*.so'); do ldd "$file" 2>/dev/null; done
    ldd "$root/$app/bin/gstpipestudio" 2>/dev/null
) | awk -v qt="$QT_DIR" 'index($3, qt) == 1 {print $3}' | sort -u | while read -r lib; do
    cp -L "$lib" "$root/$app/lib/"
done

# Find the libraries inside the package, not in the build tree, and keep the files small.
patchelf --set-rpath '$ORIGIN/../lib' "$root/$app/bin/gstpipestudio"
for tool in gps-inspect gps-update gps-run; do patchelf --remove-rpath "$root/$app/bin/$tool"; done
patchelf --remove-rpath "$root/usr/lib/$MULTIARCH/gstreamer-1.0/libgstgpsconf.so"
for lib in "$root/$app/lib/"*.so*; do patchelf --set-rpath '$ORIGIN' "$lib"; done
for plugin in $(find "$root/$app/plugins" -name '*.so'); do patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"; done
find "$root" -type f -exec sh -c 'head -c4 "$1" | grep -q "ELF"' _ {} \; -print | xargs strip --strip-unneeded

# desktop entry, icons, documentation
install -Dm644 "$here/gstpipestudio.desktop" "$root/usr/share/applications/gstpipestudio.desktop"
mkdir -p "$root/usr/share/icons" && cp -r "$here/icons/hicolor" "$root/usr/share/icons/"
doc="$root/usr/share/doc/gstpipestudio"
install -d "$doc"
install -m644 "$repo/README.md" "$doc/README.md"
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: GstPipeStudio"
    echo "Source: https://github.com/mevlt01001/GstPipeStudio"
    echo
    echo "Files: *"
    echo "Copyright: 2026 GstPipeStudio contributors"
    echo "License: MIT"
    sed -e '1,2d' -e 's/^$/./' -e 's/^/ /' "$repo/LICENSE"
    echo
    echo "Files: usr/lib/gstpipestudio/lib/libQt6* usr/lib/gstpipestudio/plugins/*"
    echo "Copyright: The Qt Company Ltd. and other contributors"
    echo "License: LGPL-3"
    echo " These are the unmodified Qt 6.8.3 shared libraries and plugins, kept in their own folder, so you can"
    echo " replace them with your own build of Qt 6.8. Source: https://download.qt.io/official_releases/qt/6.8/6.8.3/"
    echo " The full text of the license is in /usr/share/common-licenses/LGPL-3 and /usr/share/common-licenses/GPL-3."
    echo
    echo "Files: usr/lib/gstpipestudio/lib/libicu*"
    echo "Copyright: Unicode, Inc. and others"
    echo "License: Unicode"
    echo " The ICU libraries that Qt uses. License: https://www.unicode.org/license.txt"
} > "$doc/copyright"

# ---- control file: the run-time dependencies are worked out from the real binaries
mkdir -p "$WORK/shlibs/debian" && printf 'Source: gstpipestudio\n\nPackage: gstpipestudio\nArchitecture: any\n' > "$WORK/shlibs/debian/control"
elf="$(find "$root" -type f \( -path "*/bin/*" -o -name '*.so*' \) -exec sh -c 'head -c4 "$1" | grep -q "ELF"' _ {} \; -print | sed 's/^/-e/')"
shlibs="$(cd "$WORK/shlibs" && dpkg-shlibdeps -O -l"$root/$app/lib" $elf 2>/dev/null | sed -n 's/^shlibs:Depends=//p')"
[ -n "$shlibs" ] || { echo "dpkg-shlibdeps found no dependencies: something is wrong"; exit 1; }
mkdir -p "$root/DEBIAN"
size="$(du -sk "$root" | cut -f1)"
cat > "$root/DEBIAN/control" <<EOF
Package: gstpipestudio
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: GstPipeStudio contributors <mevlutbasaran01@gmail.com>
Installed-Size: ${size}
Depends: ${shlibs}, gstreamer1.0-plugins-base, fonts-dejavu-core
Recommends: gstreamer1.0-plugins-good, gstreamer1.0-plugins-bad
Section: video
Priority: optional
Homepage: https://github.com/mevlt01001/GstPipeStudio
Description: Visual designer for GStreamer and DeepStream media pipelines
 Build a media pipeline by clicking: pick an element for a free output, set its
 properties, and the pipeline is checked by really building it with GStreamer.
 The result runs with gps-run or as one element in gst-launch-1.0. Works with
 the GStreamer on the system, DeepStream elements included.
EOF

mkdir -p "$OUT"
dpkg-deb --root-owner-group -Zxz --build "$root" "$OUT/$NAME"
echo "built $OUT/$NAME"
dpkg-deb --info "$OUT/$NAME" | sed -n '/Depends/p'
