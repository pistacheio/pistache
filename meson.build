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
add_project_arguments(compiler.get_supported_arguments(['-Wconversion', '-Wno-missing-field-initializers']), language: 'cpp')

# No need for --coverage, since b_coverage is set
if get_option('b_coverage')
	add_project_arguments(compiler.get_supported_arguments(['-fstack-protector-all', '--param=ssp-buffer-size=4']), language: 'cpp')
endif

deps_libpistache = [dependency('threads'), dependency('RapidJSON', fallback: ['rapidjson', 'rapidjson_dep'])]

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
