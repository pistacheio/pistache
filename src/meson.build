# SPDX-FileCopyrightText: 2021 Andrea Pappacoda
#
# SPDX-License-Identifier: Apache-2.0

pistache_common_src = [
	'common'/'base64.cc',
	'common'/'cookie.cc',
	'common'/'description.cc',
	'common'/'eventmeth.cc',
	'common'/'http.cc',
	'common'/'http_defs.cc',
	'common'/'http_header.cc',
	'common'/'http_headers.cc',
	'common'/'mime.cc',
	'common'/'net.cc',
	'common'/'os.cc',
	'common'/'peer.cc',
	'common'/'pist_clock_gettime.cc',
        'common'/'pist_fcntl.cc',
        'common'/'pist_filefns.cc',
        'common'/'pist_sockfns.cc',
        'common'/'pist_ifaddrs.cc',
        'common'/'pist_resource.cc',
        'common'/'pist_strerror_r.cc',
        'common'/'ps_basename.cc',
        'common'/'ps_sendfile.cc',
        'common'/'ps_strl.cc',
	'common'/'reactor.cc',
	'common'/'stream.cc',
	'common'/'string_logger.cc',
	'common'/'tcp.cc',
	'common'/'timer_pool.cc',
	'common'/'transport.cc',
	'common'/'utils.cc',
        'common'/'pist_check.cc',   # Put back in correct order !!!!!!!!
        'common'/'pist_timelog.cc', # Put back in correct order        
        'common'/'pist_syslog.cc'   # Put back in correct order
]
pistache_server_src = [
	'server'/'endpoint.cc',
	'server'/'listener.cc',
	'server'/'router.cc'
]
pistache_client_src = [
	'client'/'client.cc'
]

public_args = []

if get_option('PISTACHE_USE_SSL')
	public_args += '-DPISTACHE_USE_SSL'
endif

if get_option('PISTACHE_USE_CONTENT_ENCODING_BROTLI')
	public_args += '-DPISTACHE_USE_CONTENT_ENCODING_BROTLI'
endif

if get_option('PISTACHE_USE_CONTENT_ENCODING_DEFLATE')
	public_args += '-DPISTACHE_USE_CONTENT_ENCODING_DEFLATE'
endif

if get_option('PISTACHE_DEBUG')
        add_project_arguments('-DDEBUG=1', language: 'cpp')
endif

if get_option('PISTACHE_LOG_AND_STDOUT')
        add_project_arguments('-DPS_LOG_AND_STDOUT=true', language: 'cpp')
endif

if get_option('PISTACHE_FORCE_LIBEVENT')
        add_project_arguments('-DPISTACHE_FORCE_LIBEVENT', language: 'cpp')
endif


libpistache = library(
	'pistache',
	sources: pistache_common_src + pistache_server_src + pistache_client_src,
	cpp_args: public_args,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	install: get_option('PISTACHE_INSTALL'),
	version: version_str,
	pic: get_option('b_staticpic')
)

pistache_dep = declare_dependency(
	compile_args: public_args,
	include_directories: incl_pistache,
	link_with: libpistache,
	dependencies: public_deps
)

if meson.version().version_compare('>=0.54.0')
	meson.override_dependency('libpistache', pistache_dep)
endif

import('pkgconfig').generate(
	libpistache,
	name: 'Pistache',
	description: 'An elegant C++ REST framework',
	url: 'https://pistacheio.github.io/pistache/',
	version: '@0@-git@1@'.format(version_str, version_git_date),
	filebase: 'libpistache',
	extra_cflags: public_args
)
