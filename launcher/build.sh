#!/bin/sh
# Build amatuersurgeon4_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../amatuersurgeon4_nx.nsp and ../amatuersurgeon4_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=amatuersurgeon4_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
