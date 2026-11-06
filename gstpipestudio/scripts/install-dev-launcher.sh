#!/usr/bin/env bash
# Register an already staged Development build without changing the system package.
set -euo pipefail
PRIVATE_DIR="${1:?Usage: install-dev-launcher.sh /path/to/stage/usr/lib/gstpipestudio}"
PRIVATE_DIR="$(cd "$PRIVATE_DIR" && pwd)"
BUILD_DETAILS="$(QT_QPA_PLATFORM=offscreen "$PRIVATE_DIR/bin/gstpipestudio" --version)"
case "$BUILD_DETAILS" in
    *Development*) ;;
    *) echo "Refusing a build without Development identity: $BUILD_DETAILS" >&2; exit 1 ;;
esac
python3 - "$PRIVATE_DIR" <<'PY'
import os, shlex, sys
from pathlib import Path
private = Path(sys.argv[1])
home = Path.home()
launcher = home / '.local/bin/gstpipestudio-dev'
launcher.parent.mkdir(parents=True, exist_ok=True)
launcher.write_text('#!/usr/bin/env bash\nset -e\nunset GPS_BIN_DIR QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH\nexport QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"\nexec ' + shlex.quote(str(private / 'bin/gstpipestudio')) + ' "$@"\n')
launcher.chmod(0o755)
apps = Path(os.environ.get('XDG_DATA_HOME', str(home / '.local/share'))) / 'applications'
apps.mkdir(parents=True, exist_ok=True)
quoted = str(launcher).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$')
icon = private.parents[1] / 'share/icons/hicolor/256x256/apps/gstpipestudio.png'
(apps / 'gstpipestudio-development.desktop').write_text('[Desktop Entry]\nType=Application\nName=GstPipeStudio Development\nComment=Development build with separate settings\nExec="' + quoted + '" %f\nIcon=' + str(icon) + '\nTerminal=false\nCategories=Development;\n')
old = apps / 'gstpipestudio-review.desktop'
if old.exists() and '/gstpipestudio-review/' in old.read_text():
    old.unlink()
print('Development launcher:', launcher)
PY
