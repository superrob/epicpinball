#!/bin/sh
# The settings tool (docs/config.md), in the game's folder so it finds saves/
cd "$(dirname "$0")" && exec build-linux/epicpinball-config "$@"
