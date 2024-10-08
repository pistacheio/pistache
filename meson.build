# SPDX-FileCopyrightText: 2021 Andrea Pappacoda
#
# SPDX-License-Identifier: Apache-2.0

project(
	'pistache',
	'cpp',
        version: '0.3.0', # In Meson 0.57 this can be replaced with files('version.txt')
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

fs = import('fs')

#macOS host_machine.system() is 'darwin'

if host_machine.system() != 'linux' and host_machine.system() != 'darwin' and not host_machine.system().endswith('bsd')
	error('Pistache currenly only supports Linux, macOS and Free/Open/NetBSD. See https://github.com/pistacheio/pistache/issues/6#issuecomment-242398225 for more information')
endif

compiler = meson.get_compiler('cpp')

# Wrapping arguments inside a call to get_supported_arguments so that only supported arguments get applied
# No need for -Wall -Wextra -Wpedantic, since warning_level is 3
add_project_arguments(compiler.get_supported_arguments(['-Wconversion', '-Wno-sign-conversion', '-Wno-missing-field-initializers']), language: 'cpp')

# No need for --coverage, since b_coverage is set
if get_option('b_coverage')
	add_project_arguments(compiler.get_supported_arguments(['-fstack-protector-all', '--param=ssp-buffer-size=4']), language: 'cpp')
endif

# howardhinnant/date has several names - look for them, from the most
# to the least explicit name.
# In Meson 0.60.0, this can be replaced with a simpler:
#
#     dependency('howardhinnant-date', 'hinnant-date', 'date')
#
date_dep = dependency('howardhinnant-date', required: false)
if not date_dep.found()
	date_dep = dependency('hinnant-date', required: false)
endif
if not date_dep.found()
	date_dep = dependency('date', fallback: ['hinnant-date', 'date_dep'])
endif

deps_libpistache = [
	dependency('threads'),
	date_dep
]
public_deps = []

if get_option('PISTACHE_USE_RAPIDJSON')
	rapidjson_dep = dependency('RapidJSON', fallback: ['rapidjson', 'rapidjson_dep'])
	deps_libpistache += rapidjson_dep
	public_deps += rapidjson_dep
endif

# Support Brotli compressed Content-Encoding responses...
if get_option('PISTACHE_USE_CONTENT_ENCODING_BROTLI')

	# Need Brotli encoder for library...
	brotli_dep = dependency('libbrotlienc')
	deps_libpistache += brotli_dep
	public_deps += brotli_dep
endif

# Support deflate compressed Content-Encoding responses...
if get_option('PISTACHE_USE_CONTENT_ENCODING_DEFLATE')

	# Need zlib...
	zlib_dep = dependency('zlib')
	deps_libpistache += zlib_dep
	public_deps += zlib_dep
endif

# Check if -latomic is needed - https://github.com/llvm/llvm-project/blob/main/llvm/cmake/modules/CheckAtomic.cmake
compiler_id = compiler.get_id()

cxx_atomics_check_code = '''
#include <atomic>
std::atomic<int> x;
std::atomic<short> y;
std::atomic<char> z;
int main() {
	++z;
	++y;
	return ++x;
}
'''
has_working_cxx_atomics = compiler.links(cxx_atomics_check_code, name: 'std::atomic')
if (compiler_id == 'clang' or compiler_id == 'gcc') and not has_working_cxx_atomics
	libatomic_dep = compiler.find_library('atomic')
	has_working_cxx_atomics = compiler.links(cxx_atomics_check_code, dependencies: libatomic_dep, name: 'std::atomic with libatomic')
	assert(has_working_cxx_atomics, 'Host compiler must support std::atomic')
	deps_libpistache += libatomic_dep
endif

cxx_atomics64_check_code = '''
#include <atomic>
#include <cstdint>
std::atomic<uint64_t> x (0);
int main() {
	uint64_t i = x.load(std::memory_order_relaxed);
	(void)i;
	return 0;
}
'''
has_working_cxx_atomics64 = compiler.links(cxx_atomics64_check_code, name: 'std::atomic<uint64_t>')
if (compiler_id == 'clang' or compiler_id == 'gcc') and not has_working_cxx_atomics64
	libatomic_dep = compiler.find_library('atomic')
	has_working_cxx_atomics = compiler.links(cxx_atomics64_check_code, dependencies: libatomic_dep, name: 'std::atomic<uint64_t> with libatomic')
	assert(has_working_cxx_atomics, 'Host compiler must support 64-bit std::atomic')
	deps_libpistache += libatomic_dep
endif

# Workaround https://github.com/pistacheio/pistache/issues/1068
if compiler_id == 'gcc' and compiler.version().version_compare('<9.1') or compiler_id == 'clang'
	cpp_fs_dep = compiler.find_library('stdc++fs', required: false)
	if not cpp_fs_dep.found()
		cpp_fs_dep = compiler.find_library('c++fs', required: false)
	endif
	if cpp_fs_dep.found()
		deps_libpistache += cpp_fs_dep
	endif
endif

if get_option('PISTACHE_USE_SSL')
	openssl_dep = dependency('openssl')
	deps_libpistache += openssl_dep
	public_deps += openssl_dep
endif

if host_machine.system() == 'darwin' or host_machine.system().endswith('bsd') or get_option('PISTACHE_FORCE_LIBEVENT')
	deps_libpistache += dependency('libevent')
        deps_libpistache += dependency('libevent_pthreads')
endif

if host_machine.system().endswith('bsd')
        # libexecinfo is included for the 'backtrace' function
        libexecinfo_dep = compiler.find_library('execinfo')
        deps_libpistache += libexecinfo_dep
endif

# libdl may be required for function dladdr, used in logStackTrace,
# which is called by PS_LogWoBreak, used in turn by the stack-trace
# logging macro PS_LOG_WO_BREAK_LIMITED and its derivatives. Issue
# #1230.
# Note: If 'dl' is not available, per Meson it suggests that the
# functionality is provided by libc
has_dladdr_func = compiler.has_function('dladdr')
if not has_dladdr_func
  libdl_dep += dependency('dl')
  has_dladdr_func = compiler.has_function('dladdr', dependencies: libdl_dep)
  if not has_dladdr_func
    warning('Unable to find dladdr(), even when trying to link to libdl')
  endif
  libpistache_deps += libdl_dep
endif

version_array = []
if meson.version().version_compare('>=0.57.0')
	version_array = fs.read('version.txt').strip().split('.')
else
	# Ugly workaround for reading a file
	version_array = run_command(
		find_program('python3'), '-c', 'print(open("version.txt").read())',
		check: true
	).stdout().strip().split('.')
endif

version_major    = version_array[0]
version_minor    = version_array[1]
version_patch    = version_array[2]
version_git_date = version_array[3]
version_str      = '@0@.@1@.@2@'.format(version_major, version_minor, version_patch)

version_conf = configuration_data()
version_conf.set('VERSION_MAJOR',    version_major)
version_conf.set('VERSION_MINOR',    version_minor)
version_conf.set('VERSION_PATCH',    version_patch)
version_conf.set('VERSION_GIT_DATE', version_git_date)

incl_pistache = include_directories('include')

subdir('include'/'pistache')
subdir('src')
if get_option('PISTACHE_BUILD_TESTS') and not get_option('PISTACHE_USE_RAPIDJSON')
    error('Pistache tests require rapidjson support')
endif

if get_option('PISTACHE_BUILD_TESTS')
	subdir('tests')
endif
if get_option('PISTACHE_BUILD_EXAMPLES')
	subdir('examples')
endif
if get_option('PISTACHE_BUILD_DOCS')
	subdir('docs')
endif

if not meson.is_subproject()
	git = find_program('git', required: false)
	if git.found() and fs.is_dir(meson.source_root()/'.git')
		run_command(git, 'config', '--local', 'core.hooksPath', meson.source_root()/'.hooks', check: false)
	endif
endif
