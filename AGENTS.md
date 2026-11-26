# GstPipeStudio repository instructions

Read [Coding Style](docs/development/coding-style.md) and the [architecture](docs/architecture/README.md) before changing code. The style applies to every change in this repository.

- Identify the class that owns the affected rule before editing. Search its callers across the GUI, CLI tools and GStreamer plugin. Extend the owner and migrate callers when the rule is scattered.
- Preserve the boundaries between the GUI, application use cases, Pipeline and GstEngine. Keep dependency wiring at the application entry point. Reuse the existing extension contracts before introducing one.
- Describe the effect on existing projects, templates, CLI behavior and user workflows. Obtain maintainer approval before an observable behavior change.
- Work in small commits on a separate branch. From the repository root, verify each change in the DeepStream development image.

  ```sh
  docker/dev.sh deepstream 'cmake --build build-docker-deepstream -j'
  docker/dev.sh deepstream 'cd build-docker-deepstream && ctest --output-on-failure'
  ```

- Record build failures and skipped tests distinctly. Hardware support requires a test on the relevant hardware.
