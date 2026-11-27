# GstPipeStudio Runtime SDK (`runtime_sdk`)

Standalone C99 ABI + C++17 RAII host loader for compiled GstPipeStudio pipeline `.so` artifacts.
Host applications using `runtime_sdk` have **zero dependency** on Qt, `gstpipestudio/`, or GStreamer headers.

## 5-Line Usage

```cpp
#include <gps_runtime/pipeline.hpp>

auto pipe = gps::runtime::Pipeline::open("./libmy_pipeline.so");
pipe->start();
pipe->setParam("threshold", 180);
auto m = pipe->metrics();
pipe->stop();
```

## Build & Test

```sh
cmake -S runtime_sdk -B build-runtime-sdk -G Ninja
cmake --build build-runtime-sdk -j
ctest --test-dir build-runtime-sdk --output-on-failure
```
