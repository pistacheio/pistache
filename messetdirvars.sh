#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# Use by:
#   source ./messetdirvars.sh


MY_ARCH_NM=x86
if [ "$(uname -m)" == "arm64" ]; then
    MY_ARCH_NM=a64
else
    if [ "$(uname -m)" == "aarch64" ]; then
        MY_ARCH_NM=a64
    fi
fi
    

if [ "$(uname)" == "Darwin" ]; then
    MESON_BUILD_DIR=build${MY_ARCH_NM}.mes.mac
    MESON_PREFIX_DIR=/usr/local
else
    MESON_BUILD_DIR=build${MY_ARCH_NM}.mes
    MESON_PREFIX_DIR=/usr/local
fi

