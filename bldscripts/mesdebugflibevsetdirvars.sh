#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# Use by:
#   source bldscripts/mesdebugflibevsetdirvars.sh

if [ "$(uname)" == "Darwin" ]; then
    echo "Error: Don't force libevent on macOS, libevent is on by default"
    exit 1
fi

if [ "$(uname)" == "OpenBSD" ]; then
    echo "Error: Don't force libevent on OpenBSD, libevent is on by default"
    exit 1
fi

if [ "$(uname)" == "NetBSD" ]; then
    echo "Error: Don't force libevent on NetBSD, libevent is on by default"
    exit 1
fi

PST_DIR_SUFFIX=".flibev.debug"
source bldscripts/messetdirvarsfinish.sh
