html_theme = 'https://github.com/hotdoc/hotdoc_lumen_theme/releases/download/0.6/hotdoc_lumen_theme-0.6.tar.xz?sha256=0e2f175f4cf8c00ed7ac5014e30c806a294b0d3818565eb74e1424a948e8a452'
hotdoc_p = find_program('hotdoc', required: get_option('doc'))
build_hotdoc = false

hotdoc_plugin_scanner = executable('gst-hotdoc-plugins-scanner',
  'gst-hotdoc-plugins-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gobject_dep, gmodule_dep, glib_dep, gst_dep],
  install_dir : helpers_install_dir,
  link_with: [printf_lib],
  install: true,
)

configure_file(
    input: 'gst-plugins-doc-cache-generator',
    output: 'gst-plugins-doc-cache-generator',
    copy: true,
    install_dir : helpers_install_dir,
)

plugins_cache_generator = find_program(join_paths(meson.current_build_dir(), 'gst-plugins-doc-cache-generator'))

if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

required_hotdoc_extensions = ['gi-extension']
if not import('hotdoc').has_extensions(required_hotdoc_extensions)
    if get_option('hotdoc').enabled()
        error('Documentation enabled but gi-extension missing')
    endif

    message('@0@ extensions not found, not building documentation'.format(required_hotdoc_extensions))
    subdir_done()
endif

if not build_gir
    if get_option('hotdoc').enabled()
        error('Documentation enabled but introspection not built.')
    endif

    message('Introspection not built, can\'t build the documentation')
    subdir_done()
endif

all_plugins_paths = []
foreach l: plugins
    all_plugins_paths += l.full_path()
endforeach

subdir('hotdoc-extension')

build_hotdoc = true
hotdoc = import('hotdoc')
docconf = configuration_data()
docconf.set('GST_API_VERSION', apiversion)

version_entities = configure_file(input : 'version.in',
    output : 'gst_api_version.md',
    configuration : docconf)

gst_excludes = []
foreach h: ['gettext.h', 'glib-compat-private.h', 'glib-compat.h',
            'gst-i18n-app.h', 'gst-i18n-lib.h', 'gst_private.h',
            'gstelementdetails.h', 'gstmacros.h', 'gstmarshal.h',
            'math-compat.h', 'parse/grammar.tab.h',
            'parser/grammar.tab.pre.h', 'parse/parse_lex.h', 'types.h',
            'gst-printf.h', 'printf-args.h', 'printf-extension.h',
            'printf-parse.h', 'vasnprintf.h', 'gstregistrybinary.c',
            'gstregistrybinary.h', 'gstpluginloader.h', 'gstpluginloader.c']
    gst_excludes += [join_paths(meson.current_source_dir(), '..', 'gst', h)]
endforeach

libs_doc = [hotdoc.generate_doc('gstreamer',
    project_version: apiversion,
    gi_c_sources: '../gst/*.[hc]',
    gi_sources: [gst_gir[0].full_path()],
    gi_c_source_roots: '../gst/',
    gi_c_source_filters: gst_excludes,
    sitemap: 'gst/sitemap.txt',
    index: 'gst/index.md',
    gi_index: 'gst/gi-index.md',
    gi_smart_index: true,
    gi_c_source_roots: [join_paths(meson.current_source_dir(), '../gst/'), ],
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep, hotdoc_plugin_scanner],
    extra_assets: [join_paths(meson.current_source_dir(), 'images')],
    disable_incremental_build: true,
    html_theme: html_theme,
)]

libs = [
  ['base', gst_base_gir, gst_base_dep],
  ['controller', gst_controller_gir, gst_controller_dep,],
  ['net', gst_net_gir, gst_net_dep],
  ['check', gst_check_gir, gst_check_dep],
]

foreach lib: libs
  name = lib[0]
  gir = lib[1]
  deps = [lib[2], gir]
  libs_doc += [hotdoc.generate_doc('gstreamer-' + name,
      project_version: apiversion,
      gi_c_sources: ['../libs/gst/' + name + '/*.[hc]'],
      gi_c_source_filters: gst_excludes,
      gi_sources: gir[0].full_path(),
      gi_c_source_roots: [join_paths(meson.current_source_dir(), '../libs/gst/' + name), ],
      sitemap: 'libs/' + name + '/sitemap.txt',
      index: 'libs/' + name + '/index.md',
      gi_index: 'libs/' + name + '/index.md',
      gi_smart_index: true,
      gi_order_generated_subpages: true,
      dependencies: deps + [hotdoc_extension, hotdoc_plugin_scanner],
      install: false,
      html_theme: html_theme,
  )]
endforeach

plugins = [gst_elements]
plugins_cache = join_paths(meson.current_source_dir(), 'plugins', 'gst_plugins_cache.json')
plugins_doc_dep = custom_target('build-doc-cache',
    command: [plugins_cache_generator, plugins_cache, '@INPUT@'],
    input: plugins,
    output: 'gst_plugins_cache.json',
    depends: [hotdoc_plugin_scanner],
)

plugins_doc = [hotdoc.generate_doc('GStreamer-core-plugins',
    project_version: apiversion,
    sitemap: 'plugins/sitemap.txt',
    index: 'plugins/index.md',
    gst_index: 'plugins/index.md',
    gst_smart_index: true,
    gst_c_sources: ['../plugins/elements/*.c', '../plugins/elements/*.h'],
    dependencies: [plugins_doc_dep],
    extra_extensions_paths: hotdoc_extension_dir,
    disable_incremental_build: true,
    gst_cache_file: plugins_cache,
    html_theme: html_theme,
)]