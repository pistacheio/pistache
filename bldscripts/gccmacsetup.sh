#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# This script configures gcc on macOS. You should call it in each
# terminal from which you'll build Pistache with gcc.
#   - Installs gcc if not yet installed
#   - Configures the CXX and CC environment variables
#   - Replaces brew googletest if needed and on user confirmation
#
# Use "source ..." to invoke the script, like:
#   source bldscripts/gccmacsetup.sh
#
# Add the "-y' option to proceed to replace brew googletest if needed
# without prompting for confirmation, like this:
#   source bldscripts/gccmacsetup.sh -y

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "Error: script was invoked directly, please invoke with source."
    echo "Like this:"
    echo "  source $(printf %q "$BASH_SOURCE")$((($#)) && printf ' %q' "$@")"
    exit 1
fi

if [ "$(uname)" != "Darwin" ]; then
    echo "Error: This script is for use solely on macOS"
    return 1
fi

if ! type "brew" > /dev/null; then
    echo "Error: brew command not found. Please install Homebrew."
    return 1
fi

gcc_installed_here="N"
if ! brew list gcc &>/dev/null; then
    echo "Installing gcc with brew"
    brew install gcc
    gcc_installed_here="Y"
fi

if ! [ "x$CXX" = "x" ] && [ ${#CXX} -ge 6 ]; then
    if [ ${CXX:0:4} = g++- ] || [ ${CXX:0:4} = gcc- ]; then
        if [ "x$gcc_installed_here" = "xY" ]; then
            echo "Success: gcc installed and configured"
        else
            echo "Success: gcc already configured, exiting"
        fi
        return 0
    fi
fi

gcc_lns=`brew list gcc | grep "bin/g++-[0-9][0-9]" | sort -r`
gcc_fstln=`echo "$gcc_lns" | head -1`
export CXX=`basename $gcc_fstln`
if [ "x$CXX" = "x" ] || [ ${#CXX} -lt 6 ] || ! [ ${CXX:0:4} = g++- ]; then
    echo "Error: CXX (which has value: $CXX) not configured"
    return 1
fi

gcc_lns=`brew list gcc | grep "bin/gcc-[0-9][0-9]" | sort -r`
gcc_fstln=`echo "$gcc_lns" | head -1`
export CC=`basename $gcc_fstln`
if [ "x$CC" = "x" ] || [ ${#CC} -lt 6 ] || ! [ ${CC:0:4} = gcc- ]; then
    echo "Error: CC (which has value: $CC) not configured"
    return 1
fi

echo "Success: CXX is $CXX; CC is $CC"

if brew list googletest &>/dev/null; then
    echo ""
    echo "You have brew's googletest installed"
    echo "Brew's googletest may not be compatible with building with gcc"
    replace_gt="N"
    if [ $# -ge 1 ] && [ $1 = "-y" ]; then
        replace_gt="Y"
    else
        read -e -p \
          'Replace googletest with gcc-compatible version (uses sudo)? [y/N]> '
        if [[ "$REPLY" == [Yy]* ]]; then
            replace_gt="Y"
        fi
    fi
    if [ "x$replace_gt" = "xY" ]; then
        echo "Replacing googletest"
        brew remove googletest

        git clone https://github.com/google/googletest
        cd googletest
        mkdir build
        cd build
        cmake ..
        make
        echo ""
        echo "Installing gcc-compatible googletest using \"sudo make install\""
        sudo make install
        cd "../.."
    else
        echo "You may see undefined symbols if linking Pistache's tests"
    fi
fi
