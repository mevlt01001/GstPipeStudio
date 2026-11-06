# Launch review — 2026-11-06

## Verified

| Check | Result |
|---|---|
| Public release v0.3.0 | Six .deb targets and matching checksums present. Downloaded x86_64 packages for 20.04, 22.04, 24.04 and verified checksums. |
| Public runtime image | Anonymous pull and `--version` passed for `ghcr.io/mevlt01001/gstpipestudio:0.3.0` (amd64). |
| Public dev image | Anonymous pull passed; CMake 3.22.1 and GStreamer 1.20.3 executed. |
| New runtime Dockerfile | Built locally on Ubuntu 20.04, 22.04, 24.04 bases using the matching published .deb. All started the GUI offscreen and ran the synthetic live pipeline through timeout/EOS. This tests container user spaces on the current Linux host, not three separate host kernels. |
| GUI launcher | Public 0.3.0 image opened on the local X11/XWayland desktop as UID 1000. Display-cookie forwarding worked; timeout cleanup left no container behind. Shared-folder writes retained UID/GID 1000:1000. Missing display yields an actionable error. |
| Workflow/configuration | actionlint v1.7.12 passed (external shellcheck/pyflakes disabled); all GitHub YAML parsed; shell syntax and Markdown local links checked. |
| Demo | 11.4-second silent MP4 produced from actual Qt/GStreamer captures. No camera/GPU inference claim. |

## Prepared in this change

Community conduct, security/support policies, bug/feature/hardware issue forms, PR template, beginner installation/Docker guide, authenticated-display launcher, Ubuntu runtime matrix with smoke gates, draft-safe image publishing, concise release body, Reddit copy and demo. Legacy Core/App generated diagrams and their automatic commits were removed; current Mermaid architecture remains. Design/review snapshots moved to the explicitly historical archive.

## Before posting publicly

1. Push the maintainer-reviewed commit; the new guide/forms must be reachable from the public repo. No push was made by this task.
2. Edit the existing v0.3.0 release description using `docs/releases/v0.3.0.md` (the public description was still only “GUI Improved” plus generic installation text during review). This draft was prepared locally; no authenticated release edit was performed.
3. Use existing public 0.3.0 for the announcement, or publish a new non-draft release if advertising the new Ubuntu-specific image tags. Those tags were tested locally but not pushed.
4. Recheck subreddit rules and account posting permissions, attach `docs/media/launch-demo.mp4`, use the reviewed post draft. No Reddit post or message was sent.

Jetson, DeepStream, GPU/camera passthrough, native Wayland-only desktops, macOS and Windows container GUI setups remain outside the verified launch scope. Reports are welcome through the hardware form.
