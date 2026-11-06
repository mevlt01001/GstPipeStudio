# Security policy

GstPipeStudio is early-stage software. Security fixes target the latest release and main; older releases do not have a maintenance guarantee.

Report vulnerabilities privately to **mevlutbasaran01@gmail.com** with the affected version, operating system, reproduction and likely impact. Do not open a public issue containing exploit details, credentials or private media. If GitHub private vulnerability reporting is available, you may use the repository's Security → Report a vulnerability action. We cannot promise a response deadline or a bounty.

GStreamer plugins execute native code. Pipeline files and imported launch descriptions may access files, devices and network resources and may be probed during validation. Open trusted pipelines and plugins; Docker GUI access and explicitly shared folders/devices are not a security sandbox. Share a synthetic reproducer rather than private camera credentials or recordings.
