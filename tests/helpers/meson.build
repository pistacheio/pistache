# SPDX-FileCopyrightText: 2023 Mikhail Khachayants
#
# SPDX-License-Identifier: Apache-2.0

deps_tests_helpers = [
]

if host_machine.system() == 'windows'
    # The phnt library is a collection of header files providing
    # access to certain lower-level Windows functions
    # We use it in win_fork_ish.cc, in turned used by listener_test
    
    cmake = import('cmake')

    # Configure the CMake project
    sub_proj = cmake.subproject('phnt')

    # Fetch the dependency object
    phnt_dep = sub_proj.dependency('phnt')

    deps_tests_helpers += phnt_dep

    ntdll_dep = compiler.find_library('Ntdll', required: true)
    deps_tests_helpers += ntdll_dep
endif

helpers_src = [
        'fd_utils.cc'
]

if host_machine.system() == 'windows'
    helpers_src += 'win_fork_ish.cc'
endif

tests_helpers = library(
        'tests_helpers',
        sources: helpers_src,
        dependencies: deps_tests_helpers
)

tests_helpers_dep = declare_dependency(link_with: tests_helpers)
