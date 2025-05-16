#!/bin/bash
# Bash Script (for sourcing)

#
# SPDX-FileCopyrightText: 2025 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

if [ "$(uname)" == "Darwin" ]; then
    # The OpenSSL version of libcurl is required for the Pistache test
    # https_server_test.basic_tls_requests_with_no_shutdown_from_peer. However,
    # the default libcurl on macOS uses LibreSSL, not OpenSSL. We
    # install the OpenSSL libcurl using brew.
    # See
    # https://github.com/pistacheio/pistache/pull/1310#issuecomment-2870514903
    # for more details

    if ! brew list curl &>/dev/null; then brew install curl; fi

    BREW_LIBCURL_PC="$(brew --prefix)/opt/curl/lib/pkgconfig"
    if [ ! -d "${BREW_LIBCURL_PC}" ]
    then
        echo "ERROR: libcurl pkgconfig directory not found" 1>&2
        exit 1
    fi

    if [[ ! $PKG_CONFIG_PATH == *"$BREW_LIBCURL_PC"* ]]; then
        echo "Adding libcurl pkgconfig directory to PKG_CONFIG_PATH"
        export PKG_CONFIG_PATH="$BREW_LIBCURL_PC:$PKG_CONFIG_PATH"
    fi

fi
