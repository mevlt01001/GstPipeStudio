# Live metrics examples

Open any `.conf` in GstPipeStudio, wait for validation, open **View → Live metrics**, and press **Start monitoring**. The table and canvas show every element; the graph compares all elements by default. Select one element for a focused graph. Stop monitoring before editing the graph.

`01-live-baseline.conf` is a live source, converter, queue and sink. `02-slow-stage.conf` adds an intentionally slow `identity` after a queue, making queue occupancy and source age visible. `03-qos-drops.conf` enables QoS on a synchronized sink after that slow stage; its reported drops demonstrate Drop Rate. These examples need only the standard GStreamer test plugins; no camera or GPU is required.

For a short command-line check:

```bash
gps-monitor examples/monitoring/01-live-baseline.conf --timeout-ms 3000
gps-monitor examples/monitoring/02-slow-stage.conf --timeout-ms 3000
gps-monitor examples/monitoring/03-qos-drops.conf --timeout-ms 3000
```

Each JSON line marked `type: sample` contains one 250 ms interval. `processingMs` averages completed first-sink-input → first-src-output cycles; `pendingMs` is the open cycle's wait so far. The rule applies to all elements without factory-specific branches; a source with no sink or a terminal sink with no src has no completed cycle. `dropRatePercent` is the interval ratio of QoS-reported `GST_FORMAT_BUFFERS` processed/dropped units. A missing report is unknown, not zero; reports do not prove the exact internal point where processing stopped. `ageMs` requires a live pipeline with valid PTS and segment and is pipeline running time minus output running time. `queueMs` is buffered media duration. Input and output rates are separate because transformations can change buffer counts. Telemetry never enters the saved graph.
