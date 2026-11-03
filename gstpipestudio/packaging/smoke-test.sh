#!/usr/bin/env bash
# Installs the .deb in a clean Ubuntu (no build tools, no Qt) and checks that it really works (run as root).
#
#   docker run --rm -v <outdir>:/dist:ro ubuntu:22.04 bash /dist/smoke-test.sh      (the script is copied next to the .deb)
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

deb="$(ls /dist/gstpipestudio_*.deb)"
apt-get update
# Only what the package itself asks for: if a dependency is missing, this is where it shows. Xvfb is a test tool.
apt-get install -y --no-install-recommends "$deb" xvfb xauth
step() { echo "== $*"; }

step "every library of every program resolves"
missing="$(find /usr/lib/gstpipestudio /usr/lib/*/gstreamer-1.0/libgstgpsconf.so -type f 2>/dev/null | while read -r f; do
    head -c4 "$f" | grep -q ELF && ldd "$f" 2>/dev/null | grep 'not found' | sed "s|^|$f: |"
done || true)"
[ -z "$missing" ] || { echo "$missing"; exit 1; }

step "gps-inspect version"
gps-inspect version | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["gstreamer"]["major"] == 1, d; print(d)'

step "the GStreamer plugin is found without any setting"
gst-inspect-1.0 gpsconf | grep -E 'Version|License|Origin'

step "a saved pipeline runs to its end"
cat > /tmp/smoke.conf <<'EOF'
[gstpipestudio]
version=1
name=smoke
complete=1
next=2

[element:videotestsrc_0]
factory=videotestsrc
x=0
y=0
prop.num-buffers=5

[element:fakesink_1]
factory=fakesink
x=280
y=0

[link:0]
src=videotestsrc_0
srcpad=src
sink=fakesink_1
sinkpad=sink
EOF
gps-update /tmp/smoke.conf | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["status"] == "ok", d; print("update", d["status"])'
gps-run /tmp/smoke.conf | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["status"] == "eos", d; print("run", d["status"])'

step "the window starts (headless, then on a virtual X11 screen: this loads Qt's X11 plugin)"
set +e
QT_QPA_PLATFORM=offscreen timeout 5 gstpipestudio; code=$?
[ "$code" = 124 ] || { echo "offscreen start ended with $code"; exit 1; }
xvfb-run -a timeout 5 gstpipestudio; code=$?
[ "$code" = 124 ] || { echo "X11 start ended with $code"; exit 1; }
set -e

echo "smoke test passed"
