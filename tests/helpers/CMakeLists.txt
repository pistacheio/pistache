# SPDX-FileCopyrightText: 2023 Mikhail Khachayants
#
# SPDX-License-Identifier: Apache-2.0

add_library(tests_helpers STATIC fd_utils.cc fd_utils.h)
target_compile_features(tests_helpers PRIVATE cxx_std_17)
