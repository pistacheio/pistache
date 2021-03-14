curl_dep = dependency('libcurl', required: true)
gtest_main_dep = dependency('gtest', main: true, required: true, fallback: ['gtest', 'gtest_main_dep'])

valgrind = find_program('valgrind', required: false)
gcov = find_program('gcov', required: false)
cppcheck = find_program('cppcheck', required: false)
if cppcheck.found()
	cppcheck_args = ['--enable=all','-DCPPCHECK','--suppress=*:third-party*','--suppress=*:tests*','--suppress=noExplicitConstructor:include/pistache/optional.h']
else
	warning('can\'t find Cppcheck')
endif
# TODO

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
	'string_view_test',
	'mailbox_test',
	'stream_test',
	'reactor_test',
	'threadname_test',
	'optional_test',
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
				pistache_shared_dep,
				gtest_main_dep,
				curl_dep
			]
		),
		timeout: 60,
		workdir: meson.current_build_dir(),
		is_parallel: false
	)
endforeach
