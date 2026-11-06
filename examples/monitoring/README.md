# Live metrics examples

Open either `.conf` in GstPipeStudio, wait for validation, open **View → Live metrics**, and press **Start monitoring**. Select an element and graph metric. Stop monitoring before editing the graph.

`01-live-baseline.conf` is a live source, converter, queue and sink. `02-slow-stage.conf` adds an intentionally slow `identity` after a queue, making queue occupancy and source age visible. It needs only the standard GStreamer test plugins; no camera or GPU is required.

For a short command-line check:

```bash
gps-monitor examples/monitoring/01-live-baseline.conf --timeout-ms 3000
gps-monitor examples/monitoring/02-slow-stage.conf --timeout-ms 3000
```

Each JSON line marked `type: sample` contains one 250 ms interval. `ageMs` is available only for live pipelines with valid PTS and segment. It is pipeline running time minus that buffer's output running time; it is not the element's own CPU time. `queueMs` is buffered media duration (`current-level-time`). `lossPercent` uses actual QoS processed/dropped counters when an element reports them; a dash means no trustworthy measurement. Input and output rates are separate because decoders, tees and mixers can legitimately change buffer counts. The monitor samples a running execution without writing telemetry into the saved graph.
