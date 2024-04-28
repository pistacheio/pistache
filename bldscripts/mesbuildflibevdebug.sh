#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Make debug build that forces use of libevent

# Execute this script from the parent directory by invoking:
#   bldscripts/mesbuildflibevdebug.sh

if [ "$(uname)" == "Darwin" ]; then
    source bldscripts/mesdebugsetdirvars.sh
    PFLEV=""
else
    source bldscripts/mesdebugflibevsetdirvars.sh
    PFLEV="-DPISTACHE_FORCE_LIBEVENT=true"
fi

if [ -e "./${MESON_BUILD_DIR}" ]
then
    echo "Using existing build dir ${MESON_BUILD_DIR}"
else
    meson setup ${MESON_BUILD_DIR} \
    --buildtype=debug \
    -DPISTACHE_USE_SSL=true \
    -DPISTACHE_BUILD_EXAMPLES=true \
    -DPISTACHE_BUILD_TESTS=true \
    -DPISTACHE_BUILD_DOCS=false \
    -DPISTACHE_USE_CONTENT_ENCODING_DEFLATE=true \
    -DPISTACHE_DEBUG=true \
    --prefix="${MESON_PREFIX_DIR}" \
    $PFLEV

fi

meson compile -C ${MESON_BUILD_DIR}

