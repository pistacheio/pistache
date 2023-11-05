# SPDX-FileCopyrightText: 2023 Mikhail Khachayants
#
# SPDX-License-Identifier: Apache-2.0

helpers_src = [
        'fd_utils.cc'
]

tests_helpers = library(
        'tests_helpers',
        sources: helpers_src
)

tests_helpers_dep = declare_dependency(link_with: tests_helpers)
