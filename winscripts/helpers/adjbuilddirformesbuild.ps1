# PowerShell Script

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Do not execute this script directly. In can be sourced into other
# scripts that need it.

# This script checks for the existence of a directory
# "~/mesbuild/<project-dir>"; if that directory exists,
# $MESON_BUILD_DIR is set to point to a subdirectory within it. In
# this way, if desired we can keep the build dir well away from the
# sources.

if (Test-Path -Path "$env:USERPROFILE/mesbuild") {
    $gitprojroot = git rev-parse --show-toplevel
    if (($gitprojroot) -and (Test-Path -Path "$gitprojroot")) {
        $gitprojrootleaf = Split-Path -Path "$gitprojroot" -Leaf
        $bldpathbase = "$env:USERPROFILE/mesbuild/$gitprojrootleaf"
        if (Test-Path -Path "$bldpathbase") {
            $MESON_BUILD_DIR = "$bldpathbase/$MESON_BUILD_DIR"
        }
        else {
            Write-Host "Create dir $bldpathbase if you prefer to build there"
        }
    }
}

if ((Get-Command gcc.exe -errorAction SilentlyContinue) -and `
  (("$env:CC" -eq "gcc") -or ("$env:CC" -eq "g++") -or `
  ("$env:CXX" -eq "gcc") -or ("$env:CXX" -eq "g++"))) {
      $MESON_BUILD_DIR="$MESON_BUILD_DIR.gcc"
}
