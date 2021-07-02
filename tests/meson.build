# SPDX-FileCopyrightText: 2021 Andrea Pappacoda
#
# SPDX-License-Identifier: Apache-2.0

curl_dep = dependency('libcurl')
gtest_main_dep = dependency('gtest', main: true, fallback: ['gtest', 'gtest_main_dep'])
cpp_httplib_dep = dependency('cpp-httplib', fallback: ['cpp-httplib', 'cpp_httplib_dep'])

pistache_test_files = [
	'mime_test',
	'headers_test',
	'async_test',
	'typeid_test',
	'router_test',
	'cookie_test',
	'cookie_test_2',
	'cookie_test_3',
	'view_test',
	'http_parsing_test',
	'http_uri_test',
	'http_server_test',
	'http_client_test',
	'listener_test',
	'request_size_test',
	'streaming_test',
	'rest_server_test',
	'mailbox_test',
	'stream_test',
	'reactor_test',
	'threadname_test',
	'log_api_test',
	'string_logger_test'
]
if get_option('PISTACHE_ENABLE_NETWORK_TESTS')
	pistache_test_files += 'net_test'
endif
if get_option('PISTACHE_USE_SSL')
	pistache_test_files += 'https_server_test'
	subdir('certs')
endif

foreach test_name : pistache_test_files
	test(
		test_name,
		executable(
			'run_'+test_name,
			test_name+'.cc',
			dependencies: [
				pistache_dep,
				gtest_main_dep,
				curl_dep,
				cpp_httplib_dep,
				date_dep
			]
		),
		timeout: 600,
		workdir: meson.current_build_dir(),
		is_parallel: false
	)
endforeach

cppcheck = find_program('cppcheck', required: false)
if cppcheck.found()
	cppcheck_args = [
		'--project=' + (meson.build_root()/'compile_commands.json'),
		'--enable=all',
		'-DCPPCHECK',
		'--suppress=*:*tests/*',
		'--suppress=*:*subprojects/*',
		'--std=' + get_option('cpp_std')
	]
	run_target(
		'cppcheck',
		command: [
			cppcheck,
			cppcheck_args
		]
	)
else
	warning('Can\'t find Cppcheck')
endif
