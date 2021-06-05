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

# Wrapping arguments inside a call to get_supported_arguments so that only supported arguments get applied
# No need for -Wall -Wextra -Wpedantic, since warning_level is 3
add_project_arguments(compiler.get_supported_arguments(['-Wconversion', '-Wno-sign-conversion', '-Wno-missing-field-initializers']), language: 'cpp')

# No need for --coverage, since b_coverage is set
if get_option('b_coverage')
	add_project_arguments(compiler.get_supported_arguments(['-fstack-protector-all', '--param=ssp-buffer-size=4']), language: 'cpp')
endif

deps_libpistache = [dependency('threads'), dependency('RapidJSON', fallback: ['rapidjson', 'rapidjson_dep'])]

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
has_working_cxx_atomics = compiler.compiles(cxx_atomics_check_code, name: 'std::atomic')
if (compiler_id == 'clang' or compiler_id == 'gcc') and not has_working_cxx_atomics
	libatomic_dep = compiler.find_library('atomic')
	assert(compiler.has_function('__atomic_fetch_add_4', dependencies: libatomic_dep), 'Host compiler appears to require libatomic, but cannot find it.')
	has_working_cxx_atomics = compiler.compiles(cxx_atomics_check_code, dependencies: libatomic_dep, name: 'std::atomic with libatomic')
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
has_working_cxx_atomics64 = compiler.compiles(cxx_atomics64_check_code, name: 'std::atomic<uint64_t>')
if (compiler_id == 'clang' or compiler_id == 'gcc') and not has_working_cxx_atomics64
	libatomic_dep = compiler.find_library('atomic')
	assert(compiler.has_function('__atomic_load_8', dependencies: libatomic_dep), 'Host compiler appears to require libatomic for 64-bit operations, but cannot find it.')
	has_working_cxx_atomics = compiler.compiles(cxx_atomics64_check_code, dependencies: libatomic_dep, name: 'std::atomic<uint64_t> with libatomic')
	assert(has_working_cxx_atomics, 'Host compiler must support 64-bit std::atomic')
	deps_libpistache += libatomic_dep
endif

if get_option('PISTACHE_USE_SSL')
	deps_libpistache += dependency('openssl')
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
