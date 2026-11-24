# GstPipeStudio development and build container.
# The same setup scripts as the CI workflow install GStreamer, Qt 6.8.3 (built from source) and the build tools.
#   BASE=ubuntu:22.04 (default)                         plain Ubuntu, as in CI
#   BASE=nvcr.io/nvidia/deepstream:<tag>-devel          DeepStream on top, for the GPU tests (docker/build.sh deepstream)
ARG BASE=ubuntu:22.04
FROM ${BASE}

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    QT_HOME=/opt/Qt

# A base image may ship an older CMake in /usr/local (DeepStream has 3.15); it would shadow the one apt installs.
RUN rm -rf /usr/local/bin/cmake /usr/local/bin/ctest /usr/local/bin/cpack /usr/local/bin/ccmake /usr/local/share/cmake-*

COPY gstpipestudio/scripts/check_gst.sh gstpipestudio/scripts/check_qt.sh gstpipestudio/scripts/check_build_tools.sh /scripts/

# Qt is built with at most QT_JOBS compilers at once, so the build fits in the RAM of a desktop.
ARG QT_JOBS=8
RUN /scripts/check_gst.sh && \
    taskset -c "0-$((QT_JOBS - 1))" /scripts/check_qt.sh && \
    /scripts/check_build_tools.sh && \
    rm -rf /var/lib/apt/lists/* /scripts

# Qt lives outside /root, so the container can run as the host user and leave user-owned build files.
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.8.3 \
    PATH=/opt/Qt/6.8.3/bin:${PATH}

WORKDIR /workspace
CMD ["/bin/bash"]
