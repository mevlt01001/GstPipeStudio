## Pipeline-wide live metrics

- Compare Avg. Processing Time and QoS-reported Drop Rate across all elements on the canvas and in live charts.
- Age is visible on every canvas element, including sources: pipeline running time minus output buffer running time derived from PTS. Missing timing information stays unknown.
- Inspect pending time, buffer rates, timestamp age and queue occupancy.
- Try three hardware-free monitoring examples, including a deliberately slow pipeline with reported QoS drops.

## Property editing

- Reset individual properties or all editable element properties to their factory defaults. Save applies the changes; Cancel discards them.
- Value controls accept mouse-wheel edits only after a 500 ms hover. Scrolling through a form no longer immediately changes values.

## Release workflow

- Manual releases now require a maintainer-written Markdown summary.
- GitHub appends generated pull-request notes, grouped into features, fixes and other changes.

## Measurement limits

Processing time measures first sink arrival to first src departure, including waiting. Drop Rate requires buffer-unit QoS counters; missing reports appear as unknown. QoS does not identify the exact internal processing stage where a buffer was dropped. NVIDIA/DeepStream hardware validation remains outstanding.

## Desktop experience

- Unified menu and window controls, animated About dialog, and visible version/build identity.
- Development and release builds use separate settings; support reports include the real build version and commit.
- Larger pipeline file picker, slightly lighter canvas, animated pad feedback and retracting connection previews.
- Optional connected-component dragging with one undoable position update.
- Shorter, visual README and a concise contributor ownership map with shared formatting rules.
