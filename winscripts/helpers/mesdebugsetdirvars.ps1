# PowerShell Script

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# Use by:
#   . $PSScriptRoot/helpers/mesdebugsetdirvars.sh

. $PSScriptRoot/messetdirvars.ps1

$MESON_BUILD_DIR="$MESON_BUILD_DIR.debug"
$MESON_PREFIX_DIR="$MESON_PREFIX_DIR.debug"
