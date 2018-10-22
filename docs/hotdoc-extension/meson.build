hotdoc_extension_dir = meson.current_build_dir()
config_data = configuration_data()
config_data.set('SCANNER_PATH', hotdoc_plugin_scanner.full_path())
configure_file = configure_file(input: 'setup.py', output : 'setup.py',
        configuration: config_data)
subdir('hotdoc_gst_extension')

res = run_command(python3, join_paths(meson.current_build_dir(), 'setup.py'), 'egg_info', '--egg-base',
  meson.current_build_dir())
if res.returncode() != 0
    error('Could not setup GStreamer plugins hotdoc extension: @0@'.format(res.stderr()))
endif
