# GstPipeStudio Development & Build Container
# Ubuntu 22.04 + GStreamer 1.20 + Qt 6.8.3 (built from source) + C++17 Build Tools
ARG UBUNTU=22.04
FROM ubuntu:${UBUNTU}

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8

# Run the standard project setup scripts
COPY gstpipestudio/scripts/check_gst.sh /scripts/
COPY gstpipestudio/scripts/check_qt.sh /scripts/
COPY gstpipestudio/scripts/check_build_tools.sh /scripts/

RUN /scripts/check_gst.sh && \
    /scripts/check_qt.sh && \
    /scripts/check_build_tools.sh && \
    rm -rf /var/lib/apt/lists/* /scripts

ENV CMAKE_PREFIX_PATH="/root/Qt/6.8.3"
ENV PATH="/root/Qt/6.8.3/bin:${PATH}"

WORKDIR /workspace
CMD ["/bin/bash"]

