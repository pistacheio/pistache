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

test_link_args = []
if host_machine.system() == 'windows' and compiler.get_id() == 'gcc'
    # If we don't make libstdc++ static, we leave it to the Windows OS
    # to find the dynamic libstdc++-6.dll. Normally, that would be OK,
    # since Windows checks the Path for DLL locations and we could
    # could have the gcc libstdc++-6.dll appear on the path before
    # Visual Studio's libstdc++-6.dll. However, Windows actually
    # treats libstdc++-6.dll as a "Known DLL", which means it fetches
    # it from Visual Studio regardless of what we have on the
    # Path. Which is not OK if we're building with gcc.
    # Ref: https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
    # The alternative might be do it using DLL redirection (see
    # https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-redirection)
    
    test_link_args += '-static-libgcc'
    test_link_args += '-static-libstdc++'
endif
threads_dep = dependency('threads')

foreach example_name : pistache_example_files
	executable('run'+example_name, example_name+'.cc', link_args: test_link_args, dependencies: [pistache_dep, threads_dep])
endforeach
