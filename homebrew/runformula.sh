#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# This script test the homebrew formula for Pistache. It will copy the
# local brew formula, pistache.rb, to the brew repo directory and
# install it in macOS

# Documentation: https://docs.brew.sh/Adding-Software-to-Homebrew
#                https://docs.brew.sh/Formula-Cookbook
#                https://rubydoc.brew.sh/Formula
# Also:
#  https://docs.github.com/en/repositories/releasing-projects-on-github/
#      managing-releases-in-a-repository
#
#  And to make a sha-256 from a release, Go to Pistache home page ->
#  Releases and download the tarball for the release. Then:
#    shasum -a 256 <filename.tar.gz>
#  Note: brew formula audit prefers tarball to zip file.

# For option parsing, see:
#   https://stackoverflow.com/questions/402377/using-getopts-to-process-long-and-short-command-line-options
#     (The answer that begins "The Bash builtin getopts function can be...")
#   And:
#     https://linuxsimply.com/bash-scripting-tutorial/functions/script-argument/bash-getopts/
#   MAKE SURE below that you set optspec to correctly reflect short options

MY_SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

do_error=false
do_yes=false
do_usage=false
use_head=false
skip_audit=false
audit_only=false
do_force=false
optspec=":hfy-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -)
            case "${OPTARG}" in
                HEAD)
                    use_head=true
                    ;;
                help)
                    do_usage=true
                    ;;
                skipaudit)
                    skip_audit=true
                    ;;
                auditonly)
                    audit_only=true
                    ;;
                force)
                    do_force=true
                    ;;
                *)
                    echo "Error: Unknown option --${OPTARG}" >&2
                    do_error=true
                    break
                    ;;
            esac;;
        h)
            do_usage=true
            ;;
        y)
            do_yes=true
            ;;
        f)
            do_force=true
            ;;
        *)
            echo "Error: Non-option argument: '-${OPTARG}'" >&2
            do_error=true
            break
            ;;
    esac
done

if [ "$do_error" = true ]; then do_usage=true; fi

if [ "$do_usage" = true ]; then
    echo "Usage: $(basename "$0") [-h] [--help] [-y] [--HEAD]"
    echo " -h, --help    Prints usage message, then exits"
    echo " --HEAD        Tests with head of pistache master"
    echo "               (otherwise, tests with pistache release)"
    echo " -f, --force   Test even if forumla already up-to-date"
    echo " -y            Answer yes to questions (i.e. do audit)"
    echo " --skipaudit   Skips brew audit; overrides -y for audit question"
    echo " --auditonly   Skips brew install, does audit"
    if [ "$do_yes" = true ] || [ "$do_head" = true ]; then
        echo "Error: Usage requested with other options"
        do_error=true
    fi
    if [ "$do_error" = true ]; then
        exit 1
    fi
    exit 0
fi

if ! type "brew" > /dev/null; then
    echo "brew not found; brew is required to proceed"
    exit 1
fi

export HOMEBREW_NO_INSTALL_FROM_API=1

if hbcore_repo=$(brew --repository homebrew/core); then
    if [ ! -d "$hbcore_repo" ]; then
        echo "Cloning homebrew/core to $hbcore_repo"
        brew tap --force homebrew/core
    fi
else
    echo "Cloning homebrew/core"
    brew tap --force homebrew/core
    if ! hbcore_repo=$(brew --repository homebrew/core); then
        echo "Failed to find homebrew/core repo"
        exit 1
    fi
fi

pist_form_dir="$hbcore_repo/Formula/p"
if [ ! -d "$pist_form_dir" ]; then
    echo "Failed to find homebrew/core formula dir $pist_form_dir"
    exit 1
fi
pist_form_file="$pist_form_dir/pistache.rb"

if [ -f "$pist_form_file" ]; then
    sed '1,6d' "$MY_SCRIPT_DIR/pistache.rb" >"$MY_SCRIPT_DIR/tmp_pistache.rb"
    if cmp --silent -- "$MY_SCRIPT_DIR/tmp_pistache.rb" "$pist_form_file"; then
        pistache_rb_unchanged=true
    else
        pistache_rb_unchanged=false
    fi
    rm "$MY_SCRIPT_DIR/tmp_pistache.rb"
    if [ "$pistache_rb_unchanged" == true ]; then
        if [ "$do_force" != true ]; then
            echo "$pist_form_file is already up to date, exiting"
            if [ "$use_head" != true ]; then
                if brew list pistache &>/dev/null; then
                    echo "You can try: brew remove pistache; brew install --build-from-source pistache"
                else
                    echo "You can try: brew install --build-from-source pistache"
                fi
            else
                if brew list pistache &>/dev/null; then
                    echo "You can try: brew remove pistache; brew install --HEAD pistache"
                else
                    echo "You can try: brew install --HEAD pistache"
                fi
            fi
            exit 0
        fi
    else
        pist_form_bak="$MY_SCRIPT_DIR/pistache.rb.bak"
        echo "Overwriting $pist_form_file"
        echo "Saving prior to $pist_form_bak"
        cp "$pist_form_file" "$pist_form_bak"
    fi
else
    echo "Copying $MY_SCRIPT_DIR/pistache.rb to $pist_form_file"
fi

if [ "$audit_only" != true ]; then
    # Drop copyright + license SPDX message when adding forumla to homebrew/core
    sed '1,6d' "$MY_SCRIPT_DIR/pistache.rb" >"$pist_form_file"

    if brew list pistache &>/dev/null; then brew remove pistache; fi
    if [ "$use_head" = true ]; then
        brew install --HEAD pistache
    else
        brew install --build-from-source pistache
    fi
    brew test --verbose pistache
fi

if [ "$skip_audit" != true ]; then
    do_audit=$do_yes
    if [ "$do_audit" != true ]; then
        do_audit=$audit_only
    fi
    if [ "$do_audit" != true ]; then
        read -e -p 'brew audit? [y/N]> '
        if [[ "$REPLY" == [Yy]* ]]; then
            do_audit=true
        fi
    fi

    if [ "$do_audit" = true ]; then
        echo "Auditing brew formula..."
        brew audit --strict --new --online pistache
    else
        echo "Skipping audit"
    fi
fi
