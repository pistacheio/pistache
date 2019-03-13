#!/bin/bash -e

#finding clang-format
CLANG_FORMAT=${CLANG_FORMAT:-$(which clang-format)}

#asserting clang format binnary
if [ ! -x $CLANG_FORMAT ]; then
    echo "Failed to format clang-format wasn't found."
    exit -1
fi

#finding root directory
ROOT_DIRECTORY=${ROOT_DIRECTORY:-$(realpath ./$(git rev-parse --show-cdup))}

# formatting all code
REPLACEMENTS_COUNT=$(clang-format -i -style=Mozilla $(find $ROOT_DIRECTORY -type d -name third-party* -prune -o -print | grep -E '.*\.(h|hpp|cc|cpp|cxx)$') -output-replacements-xml | grep -c "<replacement " || true)

# show number of replacements found (unformatted code)
echo "$REPLACEMENTS_COUNT format replacements found."

# this scripts fails when there are replacements to be made and succeed when there are no replacements. This indicates that the code is formatted accordingly.
if [[ $REPLACEMENTS_COUNT != 0 ]]; then
    echo "This repository wasn't formatted. Please, format using clang-format before commit"
    exit -1
else
    echo "Source code is correctly formatted"
    exit 0
fi