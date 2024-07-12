#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# NOT to be invoked directly by user. Ivoked by other set scripts once
# ... has been set
#
# Use by:
#   source bldscripts/messetdirvarsfinish.sh

MY_ARCH_NM=x86
if [ "$(uname -m)" == "arm64" ]; then
    MY_ARCH_NM=a64
else
    if [ "$(uname -m)" == "aarch64" ]; then
        MY_ARCH_NM=a64
    fi
fi
    

if [ "$(uname)" == "Darwin" ]; then
    MESON_BUILD_DIR=build${MY_ARCH_NM}.mes.mac${PST_DIR_SUFFIX}
    MESON_PREFIX_DIR=/usr/local
else
    if [[ "$OSTYPE" == "freebsd"* ]]; then
        MESON_BUILD_DIR=build${MY_ARCH_NM}.mes.fbd${PST_DIR_SUFFIX}
        MESON_PREFIX_DIR=/usr/local
    else
        if [[ "$OSTYPE" == "openbsd"* ]]; then
	    MESON_BUILD_DIR=build${MY_ARCH_NM}.mes.obd${PST_DIR_SUFFIX}
            MESON_PREFIX_DIR=/usr/local
        else
            if [[ "$OSTYPE" == "netbsd"* ]]; then
                MESON_BUILD_DIR=build${MY_ARCH_NM}.mes.nbd${PST_DIR_SUFFIX}
                MESON_PREFIX_DIR=/usr/local
            else
                MESON_BUILD_DIR=build${MY_ARCH_NM}.mes${PST_DIR_SUFFIX}
                MESON_PREFIX_DIR=/usr/local
            fi
        fi
    fi
fi

