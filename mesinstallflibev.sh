#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Install debug pistache build

source ./mesdebugflibevsetdirvars.sh

# Installs to /usr/local

sudo meson install -C ${MESON_BUILD_DIR}
