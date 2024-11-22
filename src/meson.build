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
	'common'/'pist_check.cc',
	'common'/'pist_clock_gettime.cc',
	'common'/'pist_fcntl.cc',
	'common'/'pist_filefns.cc',
	'common'/'pist_sockfns.cc',
	'common'/'pist_syslog.cc',
	'common'/'pist_ifaddrs.cc',
	'common'/'pist_resource.cc',
	'common'/'pist_strerror_r.cc',
	'common'/'pist_timelog.cc',
	'common'/'ps_basename.cc',
	'common'/'ps_sendfile.cc',
	'common'/'ps_strl.cc',
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

public_args = []

if get_option('PISTACHE_USE_SSL')
	public_args += '-DPISTACHE_USE_SSL'
endif

if get_option('PISTACHE_USE_CONTENT_ENCODING_BROTLI')
	public_args += '-DPISTACHE_USE_CONTENT_ENCODING_BROTLI'
endif

if get_option('PISTACHE_USE_CONTENT_ENCODING_ZSTD')
	public_args += '-DPISTACHE_USE_CONTENT_ENCODING_ZSTD'
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

# To add symbols to release MSVC build in Windows:
#  add_project_arguments('-Zi', language: 'cpp')
#  add_project_arguments('-O2', language: 'cpp') # May not be needed
# See also the later additions needed for libpistache_links_args
# NB: We do NOT do
#   DON'T: "add_project_arguments('-MDd', language: 'cpp')"
# We leave the release '-Md' option. '-MD' means build DLL, '-MDd'
# means build debug DLL. In cases of interest, '-MDd' hides crashes
# that will show up with '-MD'. But you still have symbols and source
# with '-MD'.

# generate Windows import library (.lib)

# implib: true is not accepted by "library" or "shared_library",
# though it is accepted by "executable"

# Useful info here:
# https://stackoverflow.com/questions/225432/export-all-symbols-when-creating-a-dll

libpistache_gen_cts = [] # generated targets, e.g. pistache.lib in Windows
libpistache_links_args = []
pistache_extra_src = []

if host_machine.system() == 'windows'

    # Here we are a taking a Windows "manifest" file
    # (pist_winlog.man), an XML file that defines the logging objects to be
    # used by Pistache in Windows, and using it to generate:
    #    1) pist_winlog.h, C hdr included by Pistache logging code
    #    2) pist_winlog.res, Windows resource file linked with pistache.dll
    #    3) pistachelog.dll, DLL used when installing logging manifest/template
    #   (we also generate pist_winlog.rc, a purely intermediate file)
    #
    # The causal chain that generates those files is:
    #   pist_winlog.man
    #         |
    #      *mc.exe*
    #         |
    #         pist_winlog.rc + pist_winlog.h
    #               |
    #            *rc.exe* or (if using gcc) windres.exe
    #               |
    #               pist_winlog.res
    #                     |
    #                   *link*
    #                     |
    #                     pistachelog.dll
    #   mc.exe and rc.exe being utilities provided by Windows

    mc_prog = find_program('mc.exe')
    if compiler.get_id() == 'gcc'
        rc_prog = find_program('windres.exe')
    else
        rc_prog = find_program('rc.exe')
    endif

    gen_src_log_rc_ct = custom_target(
      'gen-log-rc',
      input: ['winlog'/'pist_winlog.man'],
      output: ['pist_winlog.rc', 'pist_winlog.h'],
      command: [mc_prog, '-um', '-h', '@OUTDIR@', '-r', '@OUTDIR@', '@INPUT@']
    )

    if compiler.get_id() == 'gcc'
        gen_src_log_res_ct = custom_target('gen-log-res',
                             input: [gen_src_log_rc_ct[0]], # pist_winlog.rc
                             output: ['pist_winlog.o'],
                             command: [rc_prog, '@INPUT@', '-o', '@OUTPUT@'])
    else
        gen_src_log_res_ct = custom_target('gen-log-res',
                             input: [gen_src_log_rc_ct[0]], # pist_winlog.rc
                             output: ['pist_winlog.res'],
                             command: [rc_prog, '@INPUT@'])
    endif

    # We don't really need a pistachelog import library (.lib file) -
    # nothing is going to link to pistachelog.dll - but "meson
    # install" expects the .lib file to exist, so we create it.
    pistachelog_implib_linkarg = 'NONE'
    if compiler.get_id() == 'gcc'
        pistachelog_implib_linkarg = '-Wl,--out-implib=src/libpistachelog.dll.a'
    else
        # Using the /DEF: linker arg to create the import library (.lib)
        # pistachelog.def is an empty .def file provided in the source
        # tree

        if meson.version().version_compare('>=1.4.0')
            pistachelog_def_fl = files('winlog'/'pistachelog.def')
            pistachelog_implib_linkarg = '/DEF:' + pistachelog_def_fl[0].full_path()
        else
            pistachelog_implib_linkarg = '/DEF:' + meson.current_source_dir() + '/winlog/pistachelog.def'
        endif
    endif

    pistachelog_linkargs = []
    if compiler.get_id() == 'msvc'
        pistachelog_linkargs += '/noentry'
    endif
    pistachelog_linkargs += pistachelog_implib_linkarg

    win_log_dll = shared_library(
        'pistachelog',
        sources: [gen_src_log_res_ct],
        link_args: pistachelog_linkargs,
        install: true # pistachelog.dll's location is hardcoded in logging
                      # manifest (.man) file, so we kind of have to
                      # install it in that location if we're going to
                      # use it
    )

    # Add script to install logging manifest when "meson install..." is run
    install_headers(
	'winlog'/'pist_winlog.man', 'winlog'/'installmanatinstall.ps1',
        install_dir : 'src'/'winlog')
    meson.add_install_script('winlog'/'installmanatinstall.ps1')

    pistache_extra_src += gen_src_log_rc_ct[1] # for pist_winlog.h
endif


if host_machine.system() == 'windows'
  if compiler.get_id() == 'msvc'

    # In order for other programs to use pistache.dll, we need to
    # provide a pistache import library, pistache.lib, which provides
    # the pistache symbols that the pistache.dll-using-code can access.
    #
    # In Windows, unlike in Linux, a symbol is not exported from a
    # shared library (dll) unless explicitly defined to be an
    # export. This can be done either by adding __declspec(dllexport)
    # to each exported symbol in the C++ source, or by providing a
    # .def file that lists all the exported symbols. However, we don't
    # want to have to decorated the source code with
    # __declspec(dllexport) everywhere, nor do we want to maintain the
    # .def file manually, so we automate the creation of the .def file
    # as follows.
    #
    #   1/ Generate the pistache library in static-library form.
    #   2/ Use Window's dumpbin.exe to list all pistache's public symbols
    #   3/ Use dump2def.exe to turn the dumpbin output into a pistache.def file
    #   (dump2def is our own utility that we have made a Pistache subproject)
    #   4/ Link in pistache.def when linking pistache.dll
    #   5/ Provide pistache.def as a dependency to pistache.dll-using code
    #
    # This means that every public symbol in pistache.dll is exported,
    # just like pistache.so under linux

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

    gen_src_dump = custom_target('gen-pistachedump',
                             input: [libpistache_static], # src/libpistache.a
                             output: ['pistache.dump'],
                             command: [dumpbin_prog, '/LINKERMEMBER', '@INPUT@', '/OUT:@OUTPUT0@'],
                             depends: [libpistache_static])

    gen_src_def = custom_target('gen-pistachedef',
                             input: [gen_src_dump], # src/pistache.dump
                             output: ['pistache.def'],
                             command: [my_dump2def_exe, '@INPUT@', '@OUTPUT0@'],
                             depends: [gen_src_dump, my_dump2def_exe])

    gen_src_def_dep = declare_dependency(sources: [gen_src_def])
    deps_libpistache += gen_src_def_dep # DLL is dependent on .def file

    libpistache_links_args += '/DEF:src/pistache.def'
    libpistache_links_args += '/IGNORE:4102' # label defined but not referenced

    # Take DLL library name from the linker cmd line, not from
    # .def/.exp file Alternately, could fix this in dump2def
    # (LIBRARY statement).
    # Error LNK4070: /OUT:filename directive in .EXP differs from
    # output filename 'filename'; ignoring directive
    libpistache_links_args += '/IGNORE:4070'

  else # Not MSVC
    if compiler.get_id() == 'gcc'
        libpistache_links_args += '-Wl,--export-all-symbols'
    endif
  endif

    # To add symbols to release MSVC build in Windows:
    # libpistache_links_args += '/DEBUG'
    # libpistache_links_args += '/PDB:src/pistache-0.4.pdb'
    # See also the required "add_project_arguments(...)", above

endif

if host_machine.system() == 'windows'
    # Next we install the logging manifest and pistachelog.dll in the
    # system. We do this (install logging components alone, without
    # Pistache as a whole) as part of meson build, not just as part of
    # meson install, so that test programs can log without having to
    # install the whole, updated Pistache in the system.
    # Also of note, the logging manifest and pistachelog.dll are
    # expected to change very rarely.
    #
    # We do this pistachelog.dll and manifest install after
    # libpistache_static has compiled+linked successfully, to reduce
    # the risk of doing it prematurely i.e. at a time when there are
    # errors in the Pistache code that prevent compiling+linking from
    # completing successfully.

    powershell_prog = find_program('powershell.exe')
    instl_log_scrpt = 'NONE'
    if meson.version().version_compare('>=1.4.0')
        instl_log_scrpt_arr = files('winlog'/'installman.ps1')
        instl_log_scrpt = instl_log_scrpt_arr[0].full_path()
    else
        instl_log_scrpt = join_paths(meson.current_source_dir(),
                                     'winlog'/'installman.ps1')
    endif
    proj_bld_rt = 'NONE'
    if meson.version().version_compare('>=0.56.0')
        proj_bld_rt = meson.project_build_root()
    else
        proj_bld_rt = meson.build_root()
    endif
    gen_logging_txtfl_ct = custom_target(
      'gen-logging-txtfl',
      input: [win_log_dll, 'winlog'/'pist_winlog.man'],
      output: ['pist_winlog_inst_record.txt'],
          # Note: the output file, pist_winlog_inst_record.txt, is
          # created purely so that its date+time stamp can record when
          # the instl_log_scrpt script last ran and copied
          # pistachelog.dll to its correct location and installed the
          # corresponding logging manifest. Also, we define the
          # pist_winlog_inst_record.txt file to be dependent on the
          # manifest definition file, such that, if the manifest
          # definition file is changed, instl_log_scrpt will be rerun
          # and pistachelog.dll and the installed manifest will be
          # updated.
          # Further on, we also make library('pistache'...) (aka
          # pistache.dll) dependent on gen_logging_txtfl_ct,
          # i.e. dependent on pist_winlog_inst_record.txt, so that the
          # pistachelog.dll install and manifest install are carried
          # out as part of building library('pistache'...).
      command: [powershell_prog, '-NoProfile',
                   '-NonInteractive', '-WindowStyle', 'Hidden', '-NoLogo',
                   '-Command',
                   instl_log_scrpt,
                       '-dirinpstlogdll', proj_bld_rt,
                       '-inpstlogdll', '@INPUT0@',
                       '-inpstman', '@INPUT1@',
                       '-outpstmaninst', '@OUTPUT@'],
      depends: [win_log_dll, gen_src_log_rc_ct]
    )
    gen_logging_txtfl_dep = declare_dependency(sources: [gen_logging_txtfl_ct])
    deps_libpistache += gen_logging_txtfl_dep
endif

if host_machine.system() == 'windows' and compiler.get_id() == 'msvc'
    libpistache = library(
	'pistache',
	sources: pistache_common_src + pistache_server_src + pistache_client_src + pistache_extra_src,
	cpp_args: public_args,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	link_args: libpistache_links_args,
	install: get_option('PISTACHE_INSTALL'),
	soversion: version_major + '.' + version_minor,
	version: version_str,
        vs_module_defs: gen_src_def,
	pic: get_option('b_staticpic')
    )
else # No vs_module_defs
    libpistache = library(
	'pistache',
	sources: pistache_common_src + pistache_server_src + pistache_client_src + pistache_extra_src,
	cpp_args: public_args,
	include_directories: incl_pistache,
	dependencies: deps_libpistache,
	link_args: libpistache_links_args,
	install: get_option('PISTACHE_INSTALL'),
	soversion: version_major + '.' + version_minor,
	version: version_str,
	pic: get_option('b_staticpic')
    )
endif



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

if host_machine.system() == 'windows'
    if compiler.get_id() == 'gcc'
        my_filebase = 'libpistache'
    else
        my_filebase = 'pistache'
    endif

    import('pkgconfig').generate(
            libpistache,
            # libraries: 'pistachelog',
            #
            # I think pistachelog can be ommitted, since per meson
            # "dependencies of built libraries will be automatically
            # added... custom_target() objects are supported as long
            # as they are linkable", and pistachelog.dll is a
            # dependency of libpistache. In fact, adding pistachelog
            # to libraries may be harmful since it makes pistachelog
            # appear on the .pc file's "Libs" line, and we don't
            # actually want applications to try and link with
            # pistachelog.
            # ref: https://mesonbuild.com/Pkgconfig-module.html
            name: 'Pistache',
            description: 'An elegant C++ REST framework',
            url: 'https://pistacheio.github.io/pistache/',
            version: '@0@-git@1@'.format(version_str, version_git_date),
            filebase: my_filebase,
            extra_cflags: public_args
    )
else
    import('pkgconfig').generate(
            libpistache,
            name: 'Pistache',
            description: 'An elegant C++ REST framework',
            url: 'https://pistacheio.github.io/pistache/',
            version: '@0@-git@1@'.format(version_str, version_git_date),
            filebase: 'libpistache',
            extra_cflags: public_args
    )
endif
