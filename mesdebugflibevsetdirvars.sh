#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# Use by:
#   source ./mesdebugfrclibevsetdirvars.sh

if [ "$(uname)" == "Darwin" ]; then
    echo "Error: Don't force libevent on macOS, libevent is on by default"
    exit 1
fi

MY_ARCH_NM=x86
if [ "$(uname -m)" == "arm64" ]; then
    MY_ARCH_NM=a64
else
    if [ "$(uname -m)" == "aarch64" ]; then
        MY_ARCH_NM=a64
    fi
fi
    
MESON_BUILD_DIR=build${MY_ARCH_NM}.mes.flibev
MESON_PREFIX_DIR=/usr/local


