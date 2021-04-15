#!/bin/sh

set -eu

find_files() {
    git ls-files --cached --exclude-standard --others | grep -E '\.(cc|cpp|h)$'
}

find_files | xargs clang-format -i
