# r/computervision post draft

Suggested flair: **Showcase**. Attach `docs/media/launch-demo.mp4` as the demo; use the body below. Publish after the repository changes are pushed and the release links are verified.

## Title

I'm building an open-source visual GStreamer editor for camera/CV pipelines — looking for feedback

## Body

If you've worked on a camera or CV application, you may have spent time figuring out which GStreamer elements connect, what format is flowing between them, or why your stream is falling behind.

I'm building **GstPipeStudio**, an early-stage desktop tool to make that part easier to inspect.

**Source and downloads:** https://github.com/mevlt01001/GstPipeStudio

You can add and connect elements on a canvas, inspect caps and properties, import a gst-launch description, and run the result. Live charts compare per-element timing, timestamp age and queue occupancy. Drop Rate is shown only when an element reports buffer counts through QoS; unavailable measurements stay unknown.

The clip uses a synthetic live source and an intentionally slow stage, so you can reproduce it without a camera or GPU. It's a pipeline-building/debugging tool, not an inference framework or a guarantee that a pipeline is optimally configured.

It's written in C++/Qt 6.8 and MIT licensed. Ubuntu packages and a Linux Docker image are available. I use AI tools during development and take responsibility for the project and its verification. Jetson, DeepStream and camera-specific testing still need help from people with those devices.

I'd particularly appreciate:

- One small pipeline that fails to import, connect or run correctly, with the expected behavior.
- Feedback on whether the timing/caps views help with a real debugging task.
- Hardware/plugin compatibility reports, or focused contributions to import, UI and custom-plugin workflows.

My goal is to build a useful tool with the people who actually need it. What is the first camera/pipeline debugging task you'd want an editor like this to handle?
