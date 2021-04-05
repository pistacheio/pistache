#!/bin/sh

set -eu

if [ ! -z ${MESON_SOURCE_ROOT+x} ]; then cd "${MESON_SOURCE_ROOT}"; fi

find_files() {
    git ls-files --cached --exclude-standard --others | grep -E '\.(cc|cpp|h)$'
}

find_files | xargs clang-format -i
