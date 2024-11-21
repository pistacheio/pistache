# Bash Script (for sourcing)

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

# On macOS, it also checks if we're building with gcc, and, if we are,
# it appends ".gcc" to MESON_BUILD_DIR.

if [ ! "x$HOME" = "x" ]; then
    if [ -d "$HOME/mesbuild" ]; then
        gitprojroot=$(git rev-parse --show-toplevel)
        if [ ! "x$gitprojroot" = "x" ] && [ -d "$gitprojroot" ]; then
            gitprojrootleaf=$(basename $gitprojroot)
            bldpathbase="$HOME/mesbuild/$gitprojrootleaf"
            if [ -d "$bldpathbase" ]; then
                MESON_BUILD_DIR="$bldpathbase/$MESON_BUILD_DIR"
            else
                echo "Create dir $bldpathbase if you prefer to build there"
            fi
        fi
    fi
fi

if [ "$(uname)" = "Darwin" ]; then
    if ! [ "x$CXX" = "x" ] && [ ${#CXX} -ge 6 ]; then
        if [ ${CXX:0:4} = g++- ] || [ ${CXX:0:4} = gcc- ]; then
            MESON_BUILD_DIR="$MESON_BUILD_DIR.gcc"
        fi
    fi
fi
