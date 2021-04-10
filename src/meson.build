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

libpistache = library(
	'pistache',
	sources: pistache_common_src + pistache_server_src + pistache_client_src,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	install: get_option('PISTACHE_INSTALL'),
	version: pistache_version_str,
	soversion: pistache_soversion_str,
	pic: get_option('b_staticpic')
)

pistache_dep = declare_dependency(link_with: libpistache, include_directories: incl_pistache)

if meson.version().version_compare('>=0.54.0')
	meson.override_dependency('libpistache', pistache_dep)
endif

pkg = import('pkgconfig')
pkg.generate(
   libpistache,
   name: 'Pistache',
   description: 'An elegant C++ REST framework.',
   url: 'http://pistache.io',
   version: pistache_version_str+'-git'+version_data_conf.get_unquoted('VERSION_GIT_DATE'),
   filebase: 'libpistache'
)
