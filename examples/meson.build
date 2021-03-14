rapidjson = subproject('rapidjson')
rapidjson_dep = dependency('rapidjson', fallback: ['rapidjson', 'rapidjson_dep'], required: true)

pistache_example_files = [
	'custom_header',
	'hello_server',
	'http_client',
	'http_server_shutdown',
	'http_server',
	'rest_server'
]

foreach example_name : pistache_example_files
	executable('run'+example_name, example_name+'.cc', dependencies: pistache_static_dep)
endforeach

executable('run_rest_description', 'rest_description.cc', dependencies: [pistache_static_dep, rapidjson_dep])
