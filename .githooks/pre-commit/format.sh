
#!/bin/bash -e

#finding clang-format
CLANG_FORMAT=${CLANG_FORMAT:-$(which clang-format)}

#asserting clang format binnary
if [ ! -x $CLANG_FORMAT ]; then
    echo "Failed to format clang-format wasn't found."
    exit -1
fi

#finding root directory
ROOT_DIRECTORY=${ROOT_DIRECTORY:-$(realpath $(pwd))}

echo "selecting $ROOT_DIRECTORY to be formatted"

# formatting all code
clang-format -i -style=Mozilla $(find $ROOT_DIRECTORY -type d -name third-party* -prune -o -print | grep -E '.*\.(h|hpp|cc|cpp|cxx)$')

# adding changes to commit
git add $ROOT_DIRECTORY
