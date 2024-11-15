#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   winscripts/mesbuild.ps1

# Use "dot source" to include another file
. $PSScriptRoot/helpers/messetdirvars.ps1
. $PSScriptRoot/helpers/adjbuilddirformesbuild.ps1

if (($MESON_PREFIX_DIR) -and (-not (Test-Path -Path "$MESON_PREFIX_DIR"))) `
    {mkdir "$MESON_PREFIX_DIR"}

if ((Test-Path -Path "$MESON_BUILD_DIR") -and`
    (Test-Path -Path "$MESON_PREFIX_DIR")) {
        Write-Host "Using existing build dir $MESON_BUILD_DIR"
        Write-Host "Using existing prefix/install dir $MESON_PREFIX_DIR"
}
else {
    Write-Host "Going to use build dir $MESON_BUILD_DIR"
    Write-Host "Going to use prefix/install dir $MESON_PREFIX_DIR"
    meson.exe setup "$MESON_BUILD_DIR" `
    --buildtype=release `
    -DPISTACHE_USE_SSL=true `
    -DPISTACHE_BUILD_EXAMPLES=true `
    -DPISTACHE_BUILD_TESTS=true `
    -DPISTACHE_BUILD_DOCS=false `
    -DPISTACHE_USE_CONTENT_ENCODING_DEFLATE=true `
    -DPISTACHE_USE_CONTENT_ENCODING_BROTLI=true `
    -DPISTACHE_USE_CONTENT_ENCODING_ZSTD=true `
    --prefix="$MESON_PREFIX_DIR"
}

meson compile -C ${MESON_BUILD_DIR} # "...compile -v -C ..." for verbose
