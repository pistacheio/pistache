project(
	'pistache',
	'cpp',
	version: '0.0.002',
	license: 'Apache-2.0',
	default_options: [
		'cpp_std=c++17',
		'buildtype=release',
		'b_ndebug=if-release',
		'b_lto=false',
		'warning_level=3'
	],
	meson_version: '>=0.50.0'
)

compiler = meson.get_compiler('cpp')
compiler_id = compiler.get_id()
prefix = get_option('prefix')
pc_libs = ['-lpistache', '-pthread']

# Wrapping arguments inside a call to get_supported_arguments so that only supported arguments get applied
# No need for -Wall -Wextra -Wpedantic, since warning_level is 3
add_project_arguments(compiler.get_supported_arguments(['-Wconversion', '-Wno-missing-field-initializers']), language: 'cpp')

# No need for --coverage, since b_coverage is set
if get_option('b_coverage')
	add_global_arguments(compiler.get_supported_arguments(['-fstack-protector-all', '--param=ssp-buffer-size=4']), language: 'cpp')
endif

##### BEGIN CheckAtomic.cmake
is_libatomic_needed = false

cxx_atomics_check_code = '''
#include <atomic>
std::atomic<int> x;
int main(){return x;}
'''
has_working_cxx_atomics = compiler.compiles(cxx_atomics_check_code, name: 'std::atomic')
if (compiler_id.contains('clang') or compiler_id.contains('gcc')) and not has_working_cxx_atomics
	assert(compiler.has_function('__atomic_fetch_add_4', args: '-latomic'), 'Host compiler appears to require libatomic, but cannot find it.')
	has_working_cxx_atomics = compiler.compiles(cxx_atomics_check_code, args: '-latomic', name: 'std::atomic with libatomic')
	assert(has_working_cxx_atomics, 'Host compiler must support std::atomic!')
	is_libatomic_needed = true
endif

cxx_atomics64_check_code = '''
#include <atomic>
#include <cstdint>
std::atomic<uint64_t> x(0);
int main() {
uint64_t i = x.load(std::memory_order_relaxed);
return 0;
}
'''
has_working_cxx_atomics64 = compiler.compiles(cxx_atomics64_check_code, name: 'std::atomic<uint64_t>')
if not has_working_cxx_atomics64
	assert(compiler.has_function('__atomic_load_8', args: '-latomic'), 'Host compiler appears to require libatomic for 64-bit operations, but cannot find it.')
	has_working_cxx_atomics = compiler.compiles(cxx_atomics_check_code, args: '-latomic', name: 'std::atomic<uint64_t> with libatomic')
	assert(has_working_cxx_atomics, 'Host compiler must support 64-bit std::atomic')
	is_libatomic_needed = true
endif
##### END CheckAtomic.cmake

if is_libatomic_needed
	pc_libs += '-latomic'
endif

deps_libpistache = [dependency('threads')]

if get_option('PISTACHE_USE_SSL')
	deps_libpistache += dependency('openssl', required: true)
	pc_libs += ['-lssl', '-lcrypto']
endif

version_data_raw = ''
if meson.version().version_compare('>=0.57.0')
	fs = import('fs')
	version_data_raw = fs.read('version.txt').split('\n')
else
	# Ugly workaround for reading a file
	version_data_raw = run_command(
		find_program('python3'), '-c', 'print(open("version.txt").read())'
	).stdout().strip().split('\n')
endif
version_data_conf = configuration_data()
foreach _ : version_data_raw
	if _ != ''
		__ = _.split(' ')
		version_data_conf.set(__[0], __[-1])
	endif
endforeach

pistache_version_str = '.'.join([version_data_conf.get_unquoted('VERSION_MAJOR'), version_data_conf.get_unquoted('VERSION_MINOR'), version_data_conf.get_unquoted('VERSION_PATCH')])
pistache_soversion_str = '.'.join([version_data_conf.get_unquoted('SONAME_VERSION_MAJOR'), version_data_conf.get_unquoted('SONAME_VERSION_MINOR')])

configure_file(input: 'libpistache.pc.in', output: 'libpistache.pc', install: get_option('PISTACHE_INSTALL'), install_dir: get_option('libdir')/'pkgconfig', configuration: {
	'prefix': prefix,
	'exec_prefix': prefix/get_option('bindir'),
	'libdir': prefix/get_option('libdir'),
	'libs': ' '.join(pc_libs),
	'includedir': prefix/get_option('includedir'),
	'version': pistache_version_str+'-git'+version_data_conf.get_unquoted('VERSION_GIT_DATE')
})

incl_pistache = include_directories('include')

subdir('include')
subdir('src')
if get_option('PISTACHE_BUILD_TESTS')
	subdir('tests')
endif
if get_option('PISTACHE_BUILD_EXAMPLES')
	subdir('examples')
endif
if get_option('PISTACHE_BUILD_DOCS')
	subdir('docs')
endif
