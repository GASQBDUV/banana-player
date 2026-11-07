#!/bin/bash
# Banana Player on BananaOS's Ports shelf: roms/ports/Banana Player.sh, beside the BananaPlayer folder (the same
# folder the Store installs). The launcher is the folder's own.
exec "$(cd "$(dirname "$0")" && pwd)/BananaPlayer/launch.sh" "$@"
