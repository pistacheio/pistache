#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Run the test suite for Meson release (non debug) build

# Execute this script from the parent directory by invoking:
#   winscripts/mestest.ps1

. $PSScriptRoot/helpers/messetdirvars.ps1
. $PSScriptRoot/helpers/adjbuilddirformesbuild.ps1

if (($MESON_PREFIX_DIR) -and (-not (Test-Path -Path "$MESON_PREFIX_DIR")))
   {mkdir "$MESON_PREFIX_DIR"}

if (Test-Path -Path "$MESON_BUILD_DIR") {
    Write-Host "Using existing build dir $MESON_BUILD_DIR"
    meson test -C ${MESON_BUILD_DIR}
}
else {
    Write-Host "Build dir $MESON_BUILD_DIR doesn't exist"
}
