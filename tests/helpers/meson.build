# SPDX-FileCopyrightText: 2023 Mikhail Khachayants
#
# SPDX-License-Identifier: Apache-2.0

helpers_src = [
        'fd_utils.cc'
]

# It is easier to link with a static test-helpers library in the
# Windows case (no need to create an "import library" matching the
# DLL).
if host_machine.system() == 'windows'
    tests_helpers = static_library(
            'tests_helpers',
            sources: helpers_src
    )
else
    tests_helpers = library(
            'tests_helpers',
            sources: helpers_src
    )
endif

tests_helpers_dep = declare_dependency(link_with: tests_helpers)
