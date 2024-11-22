# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0

project(
	'dump2def',
	'cpp',
        version: '0.1.0',
	license: 'Apache-2.0',
	default_options: [
		'cpp_std=c++17',
		'buildtype=release',
		'b_ndebug=if-release',
		'b_lto=false',
		'warning_level=3'
	],
	meson_version: '>=0.53.2'
)

if host_machine.system() == 'windows'
	dump2def_exe = executable('dump2def', 'dump2def.cc', install : false)
endif


