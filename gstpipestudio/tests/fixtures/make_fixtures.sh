#!/usr/bin/env bash
# Makes the media files the tests need, with the GStreamer tools of the image.
# Usage: make_fixtures.sh <folder>
#   av.avi          jpeg video (30 frames, 160x120) and raw audio
#   video_only.avi  the same video, no audio
#   random.bin      4096 pseudo-random bytes (fixed seed), no known format
set -euo pipefail
dir="${1:?usage: make_fixtures.sh <folder>}"
mkdir -p "$dir"

# 30 frames at 30 fps = one second; audio ends with the video (num-buffers 43 x 1024 samples ~ 1 s at 44100 Hz)
gst-launch-1.0 -q \
  videotestsrc num-buffers=30 \
  ! video/x-raw,width=160,height=120,framerate=30/1 \
  ! jpegenc \
  ! mux. \
  audiotestsrc num-buffers=43 samplesperbuffer=1024 \
  ! audio/x-raw,rate=44100,channels=2,format=S16LE \
  ! mux. \
  avimux name=mux \
  ! filesink location="$dir/av.avi"

gst-launch-1.0 -q \
  videotestsrc num-buffers=30 \
  ! video/x-raw,width=160,height=120,framerate=30/1 \
  ! jpegenc \
  ! avimux \
  ! filesink location="$dir/video_only.avi"

# fixed seed: the same bytes every time, so "no typefinder knows this" is checked once and stays true
python3 -c 'import random, sys; r = random.Random(7); sys.stdout.buffer.write(bytes(r.randrange(256) for _ in range(4096)))' > "$dir/random.bin"
