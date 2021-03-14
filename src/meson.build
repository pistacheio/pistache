pistache_common_src = [
	'common'/'base64.cc',
	'common'/'cookie.cc',
	'common'/'description.cc',
	'common'/'http.cc',
	'common'/'http_defs.cc',
	'common'/'http_header.cc',
	'common'/'http_headers.cc',
	'common'/'mime.cc',
	'common'/'net.cc',
	'common'/'os.cc',
	'common'/'peer.cc',
	'common'/'reactor.cc',
	'common'/'stream.cc',
	'common'/'string_logger.cc',
	'common'/'tcp.cc',
	'common'/'timer_pool.cc',
	'common'/'transport.cc',
	'common'/'utils.cc'
]
pistache_server_src = [
	'server'/'endpoint.cc',
	'server'/'listener.cc',
	'server'/'router.cc'
]
pistache_client_src = [
	'client'/'client.cc'
]

if get_option('PISTACHE_USE_SSL')
	add_project_arguments('-DPISTACHE_USE_SSL', language: 'cpp')
endif

libpistache_static = static_library(
	'pistache',	pistache_common_src+pistache_server_src+pistache_client_src,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	install: get_option('PISTACHE_INSTALL'),
	pic: get_option('b_staticpic')
)
libpistache_shared = shared_library(
	'pistache',	pistache_common_src+pistache_server_src+pistache_client_src,
	version: pistache_version_str,
	soversion: pistache_soversion_str,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	install: get_option('PISTACHE_INSTALL')
)

pistache_static_dep = declare_dependency(link_with: libpistache_static, include_directories: incl_pistache)
pistache_shared_dep = declare_dependency(link_with: libpistache_shared, include_directories: incl_pistache)

if get_option('default_library') == 'static'
	meson.override_dependency('pistache', pistache_static_dep)
else
	meson.override_dependency('pistache', pistache_shared_dep)
endif
