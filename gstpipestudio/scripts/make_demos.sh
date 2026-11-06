#!/usr/bin/env bash
# Turn the real Qt/GStreamer walkthrough frames into small README previews and video clips.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FRAMES="${GPS_DEMO_DIR:-$ROOT/build/report/demo/frames}"
OUT="$ROOT/docs/media"
FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
mkdir -p "$OUT"

caption="fontfile=$FONT:fontsize=23:fontcolor=white:box=1:boxcolor=black@0.82:boxborderw=10:x=28:y=96"
build="scale=960:-2,drawtext=$caption:text='Start with a live source':enable='between(t,0,2.4)',drawtext=$caption:text='Add a queue and a slow stage':enable='between(t,2.4,4.8)',drawtext=$caption:text='Connect a sink and arrange the graph':enable='between(t,4.8,7.4)'"
metrics="scale=960:-2,drawtext=$caption:text='Compare timing and reported drops':enable='between(t,0,2)',drawtext=$caption:text='Queue level shows buffered media':enable='between(t,2,4)'"

ffmpeg -hide_banner -loglevel error -y -framerate 10 -start_number 0 -i "$FRAMES/%04d.png" -frames:v 74 -vf "$build" -c:v libx264 -pix_fmt yuv420p -crf 26 -movflags +faststart "$OUT/build-and-arrange.mp4"
ffmpeg -hide_banner -loglevel error -y -framerate 10 -start_number 74 -i "$FRAMES/%04d.png" -frames:v 40 -vf "$metrics" -c:v libx264 -pix_fmt yuv420p -crf 26 -movflags +faststart "$OUT/live-metrics.mp4"

for clip in build-and-arrange live-metrics; do
    ffmpeg -hide_banner -loglevel error -y -i "$OUT/$clip.mp4" -vf 'fps=8,scale=760:-1:flags=lanczos,palettegen=stats_mode=diff' "$OUT/$clip-palette.png"
    ffmpeg -hide_banner -loglevel error -y -i "$OUT/$clip.mp4" -i "$OUT/$clip-palette.png" -filter_complex '[0:v]fps=8,scale=760:-1:flags=lanczos[frames];[frames][1:v]paletteuse=dither=bayer:bayer_scale=5' "$OUT/$clip.gif"
    rm "$OUT/$clip-palette.png"
done

# A single silent walkthrough for community posts, built only from the actual app captures.
printf "file '%s/build-and-arrange.mp4'\nfile '%s/live-metrics.mp4'\n" "$OUT" "$OUT" > "$OUT/launch-concat.txt"
ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i "$OUT/launch-concat.txt" -c copy -movflags +faststart "$OUT/launch-demo.mp4"
rm "$OUT/launch-concat.txt"
