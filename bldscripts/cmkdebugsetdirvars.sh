#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets CMAKE_BUILD_DIR and CMAKE_PREFIX_DIR
#
# Use by:
#   source bldscripts/cmkdebugsetdirvars.sh


MY_ARCH_NM=x86
if [ "$(uname -m)" == "arm64" ]; then
    MY_ARCH_NM=a64
else
    if [ "$(uname -m)" == "aarch64" ]; then
        MY_ARCH_NM=a64
    fi
fi
    

if [ "$(uname)" == "Darwin" ]; then
    CMAKE_BUILD_DIR=build${MY_ARCH_NM}.cmk.mac.debug
    CMAKE_PREFIX_DIR=/usr/local
else
    CMAKE_BUILD_DIR=build${MY_ARCH_NM}.cmk.debug
    CMAKE_PREFIX_DIR=/usr/local
fi

