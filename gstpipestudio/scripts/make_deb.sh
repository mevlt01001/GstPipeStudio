#!/usr/bin/env bash
# make_deb.sh <ubuntu> <arch> – makes out/GstPipeStudio-<ubuntu>-<arch>.deb with CPack (run it in the folder that holds build/).
# It needs the folder /tmp/stage that the "Install Check" step makes (cmake --install into it), to find the system libraries
# that the package needs.
set -euo pipefail

UBUNTU=$1
ARCH=$2
NAME="GstPipeStudio-$UBUNTU-$ARCH.deb"
STAGE=/tmp/stage
PRIVATE=$STAGE/usr/lib/gstpipestudio

# dpkg-shlibdeps asks for a debian/control file in the folder it runs in; an empty one is enough
WORK="$(mktemp -d)"
mkdir "$WORK/debian"
touch "$WORK/debian/control"

# Every program and every library or plugin of the package. The Qt libraries that we carry belong to no Ubuntu package, so
# dpkg-shlibdeps would stop at them ("no dependency information found"): --ignore-missing-info lets it go on, and it
# adds only what the system libraries need.
FILES=("$PRIVATE"/bin/gstpipestudio "$PRIVATE"/bin/gps-inspect "$PRIVATE"/bin/gps-update "$PRIVATE"/bin/gps-run)
while IFS= read -r file; do FILES+=("$file"); done < <(find "$STAGE"/usr/lib -type f -name '*.so*')
DEPENDS="$(cd "$WORK" && dpkg-shlibdeps -O --ignore-missing-info -l"$PRIVATE/lib" "${FILES[@]}" | sed 's/^shlibs:Depends=//')"
echo "dependencies on system libraries: $DEPENDS"

# GStreamer is not carried: apt installs it with the package (the programs run gst-inspect-1.0 and the plugin lives in its folder)
cpack --config build/CPackConfig.cmake -G DEB -B out \
    -D CPACK_DEBIAN_FILE_NAME="$NAME" \
    -D CPACK_DEBIAN_PACKAGE_DEPENDS="gstreamer1.0-tools (>= 1.16), gstreamer1.0-plugins-base (>= 1.16), fonts-dejavu-core, $DEPENDS"

# exit code of the script: 0 = the package exists, 1 = not
test -f "out/$NAME"
dpkg-deb --info "out/$NAME"
