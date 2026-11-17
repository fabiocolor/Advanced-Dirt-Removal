#!/bin/zsh
# Resolve does not scan the per-user OFX folder unless it is on its search path.
# Quit an existing Resolve session before using this development launcher.
set -eu
open --env "OFX_PLUGIN_PATH=${OFX_PLUGIN_PATH:+$OFX_PLUGIN_PATH:}$HOME/Library/OFX/Plugins" -a 'DaVinci Resolve'
