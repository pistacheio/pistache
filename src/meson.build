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

# generate Windows import library (.lib)

# implib: true is not accepted by "library" or "shared_library",
# though it is accepted by "executable"

# Useful info here:
# https://stackoverflow.com/questions/225432/export-all-symbols-when-creating-a-dll

libpistache_gen_cts = [] # generated targets, e.g. pistache.lib in Windows
libpistache_links_args = []
pistache_extra_src = []

if host_machine.system() == 'windows'
    mc_prog = find_program('mc.exe')
    rc_prog = find_program('rc.exe')
    link_prog = find_program('link.exe')
    
    gen_src_log_rc_ct = custom_target('gen-log-rc',
                             input: ['winlog'/'pist_winlog.man'],
                             output: ['pist_winlog.rc', 'pist_winlog.h'],
                             command: [mc_prog, '-um', '-h', '@SOURCE_ROOT@/include/pistache/', '-r', '@BUILD_ROOT@/src', '@INPUT@'])

    gen_src_log_res_ct = custom_target('gen-log-res',
                             # input: ['pist_winlog.rc'],
                             output: ['pist_winlog.res'],
                             command: [rc_prog, 'src/pist_winlog.rc'],
                             depends: gen_src_log_rc_ct)

    # Note - we use output: "pistachelog.dum" here because if we use
    # "pistachelog.dll" meson decides that the dll can be linked
    # against the library, and includes it on the library link
    # path. But in fact, DLLs cannot be linked with libraries (static
    # or dynamic) in Windows, nor with executables - for that the
    # corresponding import library (.lib) must be used.
    gen_src_log_dll_ct = custom_target('gen-log-dll',
                             # input: ['pist_winlog.res'],
                             output: ['pistachelog.dum'],
                             command: [link_prog, '/dll', '/noentry', '/machine:x64', 'src/pist_winlog.res', '/OUT:src/pistachelog.dll'],
                             depends: gen_src_log_res_ct)

    pistache_extra_src += gen_src_log_rc_ct
    pistache_extra_src += gen_src_log_res_ct
    pistache_extra_src += gen_src_log_dll_ct
endif

if host_machine.system() == 'windows'
    dump2def_subproj = subproject('dump2def')
    my_dump2def_exe = dump2def_subproj.get_variable('dump2def_exe')

    libpistache_static = static_library(
	'pistache',
	sources: pistache_common_src + pistache_server_src + pistache_client_src + pistache_extra_src,
	cpp_args: public_args,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	install: false, # Could do get_option('PISTACHE_INSTALL') if preferred
	pic: get_option('b_staticpic')
)

    dumpbin_prog = find_program('dumpbin.exe')
    lib_prog = find_program('lib.exe')

    gen_src_dump = custom_target('gen-pistachedump',
                             # input: ['libpistache.a'],
                             output: ['pistache.dump'],
                             command: [dumpbin_prog, '/LINKERMEMBER', 'src/libpistache.a', '/OUT:@OUTPUT0@'],
                             depends: [libpistache_static])

    gen_src_def = custom_target('gen-pistachedef',
                             # input: ['pistache.dump'],
                             output: ['pistache.def'],
                             command: [my_dump2def_exe, 'src/pistache.dump', '@OUTPUT0@'],
                             depends: [gen_src_dump, my_dump2def_exe])

    # It is possible to invoke lib.exe to explicitly create the import
    # library, pistache.lib, as per below. However, this is
    # unnecessary - linking the DLL actually generates pistache.lib
    # as a side effect, and meson adds 'src/pistache.lib' to the list
    # of libs linked with test and example programs without us
    # explicitly defining a dependency for pistache.lib.
    #
    # pistache_dll_import_lib_ct = custom_target('gen-pistachelib',
    #                          # input: ['pistache.def'],
    #                          output: ['pistache.lib'],
    #                          command: [lib_prog, '/IGNORE:4102', '/DEF:src/pistache.def', '/OUT:@OUTPUT0@'],
    #                          depends: [gen_src_def])
    # import_lib_dep = declare_dependency(sources: [pistache_dll_import_lib_ct])
    # libpistache_gen_cts += pistache_dll_import_lib_ct

    gen_src_def_dep = declare_dependency(sources: [gen_src_def])
    deps_libpistache += gen_src_def_dep # DLL is dependent on .def file
    
    libpistache_links_args += '/DEF:src/pistache.def'
    libpistache_links_args += '/IGNORE:4102'


endif

libpistache = library(
	'pistache',
	sources: pistache_common_src + pistache_server_src + pistache_client_src + pistache_extra_src,
	cpp_args: public_args,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	link_args: libpistache_links_args,
	install: get_option('PISTACHE_INSTALL'),
	version: version_str,
	pic: get_option('b_staticpic')
)



pistache_dep = declare_dependency(
	compile_args: public_args,
	include_directories: incl_pistache,
        sources: libpistache_gen_cts,
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
