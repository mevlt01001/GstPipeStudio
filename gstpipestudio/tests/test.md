# Test plan

One place for everything that must be tested before a release. Today it holds only the GStreamer install tests.
The Qt install, the core and the app get their own sections here later, so the whole picture stays in view.

How to read the checklists: `- [ ]` is a check that still has to be written as an automated test. A note
**by hand** says the same thing was already seen in a manual run (on 2026-11-03), which is not a test yet.

---

## Gst-Test

**What is tested:** the scripts `scripts/check_gst.sh` and `scripts/install_gst_x64.sh`. After they ran, the system must
have a GStreamer that GstPipeStudio can be built against and can run with.

**Principle: test what GStreamer can do, never how many elements it has.** The number of elements is 1447 on Ubuntu 20.04
and 1631 on 24.04, and it is higher still where a company adds its own plugins. A count says nothing, so there is no check
that counts elements and no threshold.

**Where it runs:** a fresh, bare container of Ubuntu **20.04, 22.04 and 24.04** (x86-64), one container per check group, so
that nothing from an earlier check is left over. Expected GStreamer: 1.16.x, 1.20.x and 1.24.x. Tests that need the network
(`apt-get update`, the install) have a time limit so that a stuck prompt fails the test instead of hanging it.

### A. Install on a bare system

- [ ] **A1** `check_gst.sh` ends with exit code 0. *(by hand: 20.04, 22.04, 24.04)*
- [ ] **A2** The output says GStreamer is not installed, installs, then prints the version and `OK`. *(by hand)*
- [ ] **A3** Nothing waits for an answer. On 20.04 `tzdata` used to stop and ask for a time zone; the run must end by
  itself within the time limit. *(by hand: found and fixed on 20.04)*
- [ ] **A4** The version is 1.16 or newer, and it is the one this Ubuntu offers (1.16.x, 1.20.x, 1.24.x). *(by hand)*

### B. What the installed GStreamer can do (no counting)

- [ ] **B1** `gst-inspect-1.0 --version` answers. *(by hand)*
- [ ] **B2** The basic elements exist: `videotestsrc`, `fakesink`, `queue`, `tee`, `videoconvert`. Seen by hand:
  `videotestsrc` and `jpegdec`; the others are still to check.
- [ ] **B3** A pipeline really runs: `gst-launch-1.0 videotestsrc num-buffers=5 ! fakesink` ends with exit code 0 and no
  error. *(never run so far)*
- [ ] **B4** The codecs of the full install work together: encode and decode a few frames,
  `videotestsrc num-buffers=10 ! x264enc ! h264parse ! avdec_h264 ! fakesink` ends with exit code 0. Seen by hand that
  `x264enc` and `avdec_h264` exist; running them is still to check.
- [ ] **B5** The output elements exist: `alsasink`, `pulsesink`, `gtksink` (only that `gst-inspect-1.0` finds them; a container
  has no screen and no sound card, so they are not run). *(by hand: exist)*

### C. Can the project be built against it

- [ ] **C1** `pkg-config --modversion gstreamer-1.0 gstreamer-base-1.0` gives 1.16 or newer for both. *(by hand: found)*
- [ ] **C2** A tiny C program that calls `gst_init` and prints the version compiles with
  `pkg-config --cflags --libs gstreamer-1.0` and runs. This is stronger than "the header exists".
- [ ] **C3** CMake finds it with the very call the project uses: `pkg_check_modules(GST gstreamer-1.0 gstreamer-base-1.0)`.
  *(by hand: the project built on all three Ubuntus with an earlier version of the install scripts)*

### D. Second run and edge cases

- [ ] **D1** Run it a second time: exit code 0, same version, and nothing is installed again (no "Installing" in the output).
  *(by hand)*
- [ ] **D2** An old GStreamer (a fake `gst-inspect-1.0` that answers 1.14.5) gives the message
  `Your system has a GStreamer version older than 1.16 (found 1.14.5)...` and exit code 1. *(by hand)*
- [ ] **D3** The border: exactly 1.16.0 passes (seen by hand), and 1.15.9 fails.
- [ ] **D4** ARM (a fake `uname -m` that answers `aarch64`) with GStreamer missing: `install_gst_x64.sh` refuses, nothing is
  installed, exit code 1, and the message says there is no installer for this system yet. *(by hand)*
- [ ] **D5** `install_gst_x64.sh` run directly on that fake ARM system also refuses with exit code 1.
- [ ] **D6** As a normal user with `sudo` (not root): the same run works. *(never tried: all runs so far were root)*
- [ ] **D7** `apt-get update` cannot reach the network: the script stops with a clear failure and installs nothing.

### E. Known gaps (decide, then turn into checks or accept)

- [ ] **E1** `check_gst.sh` asks only whether `gst-inspect-1.0` exists. A system with only `gstreamer1.0-tools` passes,
  although it has no headers and no `videotestsrc` (measured: 28 to 30 elements, no `videotestsrc`). Either the script also
  checks the headers and the basic elements, or this is accepted. B2 and C1 will show it.
- [ ] **E2** ARM has no installer yet; there is only the refusal in D4.
- [ ] **E3** A real Jetson (JetPack and DeepStream): the installed GStreamer must not be touched. Never tried.
- [ ] **E4** `scripts/check-gstreamer.sh` does the same job as `check_gst.sh`. Keep one of them.

---

## Later

The Qt install, the core, and the app get their own sections in this file, in this order, once the GStreamer part is done.
