#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

source ./messetdirvars.sh
if [ -e "./${MESON_BUILD_DIR}" ]
then
    rm -rf ${MESON_BUILD_DIR}
fi

source ./mesdebugsetdirvars.sh
if [ -e "./${MESON_BUILD_DIR}" ]
then
    rm -rf ${MESON_BUILD_DIR}
fi

source ./cmksetdirvars.sh
if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    rm -rf ${CMAKE_BUILD_DIR}
fi

source ./cmkdebugsetdirvars.sh
if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    rm -rf ${CMAKE_BUILD_DIR}
fi
