doxygen = find_program('doxygen', required: true)
configure_file(input: 'Doxyfile.in', output: 'Doxyfile', configuration: {
	'CMAKE_CURRENT_BINARY_DIR': meson.current_build_dir(),
	'CMAKE_SOURCE_DIR': meson.project_source_root(),
	'PROJECT_NAME': meson.project_name()
})

run_target('docs', command: [doxygen, meson.current_build_dir()/'Doxyfile'])
