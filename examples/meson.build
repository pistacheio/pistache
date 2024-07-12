# SPDX-FileCopyrightText: 2021 Andrea Pappacoda
#
# SPDX-License-Identifier: Apache-2.0

pistache_example_files = [
	'custom_header',
	'hello_server',
	'http_client',
	'http_server_shutdown',
	'http_server',
	'rest_server',
	'rest_description'
]

threads_dep = dependency('threads')

# In OpenBSD, rapidjson header fles are installed in
# /usr/local/include, not in /usr/include. Consequently, we need
# deps_libpistache in our dependencies, not just pistache_dep
#
# In particular, for rapidjson/prettywriter.h in rest_description.cc

foreach example_name : pistache_example_files
	executable('run'+example_name, example_name+'.cc', dependencies: [pistache_dep, threads_dep, deps_libpistache])
endforeach
