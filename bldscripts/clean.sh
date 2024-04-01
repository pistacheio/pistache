#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   bldscripts/clean.sh

source bldscripts/messetdirvars.sh
if [ -e "./${MESON_BUILD_DIR}" ]
then
    rm -rf ${MESON_BUILD_DIR}
fi

source bldscripts/mesdebugsetdirvars.sh
if [ -e "./${MESON_BUILD_DIR}" ]
then
    rm -rf ${MESON_BUILD_DIR}
fi

source bldscripts/cmksetdirvars.sh
if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    rm -rf ${CMAKE_BUILD_DIR}
fi

source bldscripts/cmkdebugsetdirvars.sh
if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    rm -rf ${CMAKE_BUILD_DIR}
fi
