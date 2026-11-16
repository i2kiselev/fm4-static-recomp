#!/bin/sh
# Starts the launcher inside the Flatpak (installed as /app/bin/fm4-launcher).
# The bundled clang comes first on PATH; .NET unpacks its native libraries into the app's cache,
# because the home folder is mounted read-only.
export PATH="/app/llvm/bin:$PATH"
export DOTNET_BUNDLE_EXTRACT_BASE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/dotnet-bundle"
exec /app/launcher/FM4StaticRecompLauncher "$@"
