#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Run the test suite for Meson non-debug (release) build

# Execute this script from the parent directory by invoking:
#   bldscripts/mestest.sh

source bldscripts/messetdirvars.sh

if [ -e "./${MESON_BUILD_DIR}" ]
then
    echo "Using build dir ${MESON_BUILD_DIR}"
    meson test -C ${MESON_BUILD_DIR}
else
    echo "Build dir ${MESON_BUILD_DIR} doesn't exist"
    exit 1
fi


