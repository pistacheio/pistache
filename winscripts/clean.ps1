#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   winscripts/clean.ps1

# Use "dot source" to include another file
. $PSScriptRoot/helpers/messetdirvars.ps1
. $PSScriptRoot/helpers/adjbuilddirformesbuild.ps1

if (($MESON_BUILD_DIR) -and (Test-Path -Path "$MESON_BUILD_DIR")) {
    Write-Host "Removing build dir $MESON_BUILD_DIR"
    Remove-Item "$MESON_BUILD_DIR" -Recurse -Force
}
else {
    Write-Host "Build dir $MESON_BUILD_DIR not found"
}

. $PSScriptRoot/helpers/mesdebugsetdirvars.ps1
. $PSScriptRoot/helpers/adjbuilddirformesbuild.ps1

if (($MESON_BUILD_DIR) -and (Test-Path -Path "$MESON_BUILD_DIR")) {
    Write-Host "Removing debug build dir $MESON_BUILD_DIR"
    Remove-Item "$MESON_BUILD_DIR" -Recurse -Force
}
else {
    Write-Host "Debug build dir $MESON_BUILD_DIR not found"
}
