# -*- coding: utf-8 -*-
#
# Copyright © 2018 Thibault Saunier <tsaunier@igalia.com>
#
# This library is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2.1 of the License, or (at your option)
# any later version.
#
# This library is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library.  If not, see <http://www.gnu.org/licenses/>.

import tempfile
import re
import os
from collections import Mapping
from collections import OrderedDict

import html
from wheezy.template.engine import Engine
from wheezy.template.ext.core import CoreExtension
from wheezy.template.ext.code import CodeExtension
from wheezy.template.loader import FileLoader
from hotdoc.extensions import gi
from hotdoc.extensions.gi.gi_extension import GIExtension
from hotdoc.core.links import Link
from hotdoc.utils.loggable import warn, info, Logger, error
from hotdoc.core.extension import Extension
from hotdoc.core.symbols import ClassSymbol, QualifiedSymbol, PropertySymbol, SignalSymbol, ReturnItemSymbol, ParameterSymbol, Symbol
from hotdoc.parsers.gtk_doc import GtkDocParser
from hotdoc.extensions.c.utils import CCommentExtractor
from hotdoc.core.exceptions import HotdocSourceException
from hotdoc.core.formatter import Formatter
from hotdoc.core.comment import Comment
from hotdoc.extensions.gi.gi_extension import WritableFlag, ReadableFlag, ConstructFlag, ConstructOnlyFlag
from hotdoc.extensions.gi.fundamentals import FUNDAMENTALS

import subprocess
import json

Logger.register_warning_code('signal-arguments-mismatch', HotdocSourceException,
                             'gst-extension')

DESCRIPTION=\
        """
Extract gstreamer plugin documentation from sources and
built plugins.
"""
CACHE_FIXED_DEFAULT="hotdoc-fixed-default"


def _cleanup_package_name(package_name):
    return package_name.strip(
        'git').strip('release').strip(
            'prerelease').strip(
                'GStreamer').strip(
                    'Plug-ins').strip(' ')

class GstPluginsSymbol(Symbol):
    TEMPLATE = """
        @require(symbol, has_unique_feature)
        @if not has_unique_feature:
            <div class="base_symbol_container">
            <table class="table table-striped table-hover">
                <tbody>
                    <tr>
                        <th class="col-md-2"><b>Name</b></th>
                        <th class="col-md-2"><b>Classification</b></th>
                        <th class="col-md-4"><b>Description</b></th>
                    </tr>
                    @for elem in symbol.get_elements():
                        <tr>
                            <td>@elem.rendered_link</td>
                            <td>@elem.classification.replace('/', ' ')</td>
                            <td>@elem.desc</td>
                        </tr>
                    @end
                    @end
                </tbody>
            </table>
            </div>
            @end
        @end
        """
    __tablename__ = 'gst_plugins'

    def __init__(self, **kwargs):
        self.name = None
        self.description = None
        self.plugins = []
        self.elems = []
        self.all_plugins = False
        Symbol.__init__(self, **kwargs)

    def get_elements(self):
        all_elements = []
        for plugin in self.plugins:
            for elem in plugin.elements:
                all_elements.append(elem)

        return sorted(all_elements, key=lambda x: x.display_name)

    def get_children_symbols(self):
        return self.plugins

    @classmethod
    def get_plural_name(cls):
        return ""


class GstElementSymbol(ClassSymbol):
    TEMPLATE = """
        @require(symbol, hierarchy, desc)
        @desc

        @if hierarchy:
        <h2>Hierarchy</h2>
        <div class="hierarchy_container">
            @hierarchy
        </div>
        @end
        <h2 class="symbol_section">Factory details</h2>
        <div class="symbol-detail">
            <p><b>Authors:</b> – @symbol.author</p>
            <p><b>Classification:</b> – <code>@symbol.classification</code></p>
            <p><b>Rank</b> – @symbol.rank</p>
            <p><b>Plugin</b> – @symbol.plugin</p>
            <p><b>Package</b> – @symbol.package</p>
        </div>
        @end
        """
    __tablename__ = 'gst_element'

    def __init__ (self, **kwargs):
        self.classification = None
        self.rank = None
        self.author = None
        self.plugin = None
        ClassSymbol.__init__(self, **kwargs)

    @classmethod
    def get_plural_name(cls):
        return ""

class GstPluginSymbol(Symbol):
    TEMPLATE = """
        @require(symbol, has_unique_feature)
        <h1>@symbol.display_name</h1>
        <i>(from @symbol.package)</i>
        <div class="base_symbol_container">
        @symbol.formatted_doc
        @if not has_unique_feature:
        <table class="table table-striped table-hover">
            <tbody>
                <tr>
                    <th><b>Name</b></th>
                    <th><b>Classification</b></th>
                    <th><b>Description</b></th>
                </tr>
                @for elem in symbol.elements:
                    <tr>
                        <td>@elem.rendered_link</td>
                        <td>@elem.classification</td>
                        <td>@elem.desc</td>
                    </tr>
                @end
            </tbody>
        </table>
        @end
        </div>
        @end
        """
    __tablename__ = 'gst_plugin'

    def __init__ (self, **kwargs):
        self.name = None
        self.license = None
        self.description = None
        self.package = None
        self.elements = []
        Symbol.__init__ (self, **kwargs)

    def get_children_symbols(self):
        return self.elements

    @classmethod
    def get_plural_name(cls):
        return ""


class NamedConstantsSymbols(Symbol):
    __tablename__ = 'named constants'

    def __init__(self, **kwargs):
        self.members = {}
        self.raw_text = ''
        self.anonymous = False
        Symbol.__init__(self, **kwargs)

    def get_children_symbols(self):
        return self.members

    def get_extra_links(self):
        return [m.link for m in self.members]

    def get_type_name(self):
        return "Name constant"

    @classmethod
    def get_plural_name(cls):
        return 'Named constants'

class GstPadTemplateSymbol(Symbol):
    TEMPLATE = """
        @extends('base_symbol.html')
        @require(symbol)

        @def header():
        <h3><span><code>@symbol.name</code></span></h3>
        @end
        @def content():
        <div>
            <pre class="language-yaml"><code class="language-yaml">@symbol.caps</code></pre>
            <div class="symbol-detail">
                <p><b>Presence</b> – <i>@symbol.presence</i></p>
                <p><b>Direction</b> – <i>@symbol.direction</i></p>
            </div>
        </div>
        @end
        """

    __tablename__ = 'pad_templates'

    def __init__(self, **kwargs):
        self.qtype = None
        self.name = None
        self.direction = None
        self.presence = None
        self.caps = None
        Symbol.__init__(self, **kwargs)

    def get_children_symbols(self):
        return [self.qtype]

    # pylint: disable=no-self-use
    def get_type_name(self):
        """
        Banana banana
        """
        return "GstPadTemplate"


ENUM_TEMPLATE = """
@require(values)
<div class="symbol-detail">
@for value in values:
    <p><code>@value['name']</code> (<i>@value['value']</i>) – @value['desc']</p>
@end
</div>
"""


class GstFormatter(Formatter):
    engine = None

    def __init__(self, extension):
        self.__tmpdir = tempfile.TemporaryDirectory()
        with open(os.path.join(self.__tmpdir.name, "padtemplate.html"), "w") as f:
            f.write(GstPadTemplateSymbol.TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "enumtemplate.html"), "w") as f:
            f.write(ENUM_TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "plugins.html"), "w") as f:
            f.write(GstPluginsSymbol.TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "plugin.html"), "w") as f:
            f.write(GstPluginSymbol.TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "element.html"), "w") as f:
            f.write(GstElementSymbol.TEMPLATE)
        Formatter.__init__(self, extension)
        self._ordering.insert(0, GstPluginSymbol)
        self._ordering.insert(1, GstElementSymbol)
        self._ordering.insert(self._ordering.index(ClassSymbol) + 1, GstPadTemplateSymbol)
        self._ordering.insert(self._ordering.index(GstPadTemplateSymbol) + 1,
                              GstPluginsSymbol)
        self._ordering.append(NamedConstantsSymbols)

        self._symbol_formatters.update(
                {GstPluginsSymbol: self._format_plugins_symbol,
                 GstPluginSymbol: self._format_plugin_symbol,
                 GstPadTemplateSymbol: self._format_pad_template_symbol,
                 GstElementSymbol: self._format_element_symbol,
                 NamedConstantsSymbols: self._format_enum,
                })

    def get_template(self, name):
        return GstFormatter.engine.get_template(name)

    def parse_toplevel_config(self, config):
        super().parse_toplevel_config(config)
        if GstFormatter.engine is None:
            gi_extension_path = gi.__path__[0]

            searchpath = [os.path.join(gi_extension_path, "html_templates"), self.__tmpdir.name] + Formatter.engine.loader.searchpath
            GstFormatter.engine = Engine(
                    loader=FileLoader(searchpath, encoding='UTF-8'),
                    extensions=[CoreExtension(), CodeExtension()])
            GstFormatter.engine.global_vars.update({'e': html.escape})

    def __del__(self):
        self.__tmpdir.cleanup()


    def __populate_plugin_infos(self, plugin, render_package=False):
        if not plugin.description:
            comment = self.extension.app.database.get_comment(plugin.unique_name)
            plugin.description = comment.description if comment else ''
        plugin.rendered_link = self._format_linked_symbol(plugin)
        for element in plugin.elements:
            comment = self.extension.app.database.get_comment(
                'element-' + element.unique_name)

            element.plugin_rendered_link = plugin.rendered_link
            element.license = plugin.license
            element.rendered_link = self._format_linked_symbol(element)
            if not comment:
                element.desc  = "%s element" % element.display_name
                continue

            if not comment.short_description:
                desc = "%s element" % (element.display_name)
            else:
                desc = comment.short_description.description
            element.desc = desc

    def _format_prototype(self, func, is_pointer, title):
        c_proto = Formatter._format_prototype (self, func, is_pointer, title)
        template = self.get_template('python_prototype.html')
        python_proto = template.render(
                {'function_name': title,
                 'parameters': func.parameters,
                 'throws': False,
                 'comment': "python callback for the '%s' signal" % func._make_name(),
                 'is_method': False})
        template = self.get_template('javascript_prototype.html')
        for param in func.parameters:
            param.extension_contents['type-link'] = self._format_linked_symbol (param)
        js_proto = template.render(
                {'function_name': title,
                 'parameters': func.parameters,
                 'throws': False,
                 'comment': "javascript callback for the '%s' signal" % func._make_name(),
                 'is_method': False})
        for param in func.parameters:
            param.extension_contents.pop('type-link', None)
        return '%s%s%s' % (c_proto, python_proto, js_proto)

    def _format_plugins_symbol(self, symbol):
        for plugin in symbol.plugins:
            self.__populate_plugin_infos(plugin, symbol.all_plugins)
        template = self.engine.get_template('plugins.html')
        return template.render ({'symbol': symbol,
                                 'has_unique_feature': self.extension.has_unique_feature})

    def _format_plugin_symbol(self, symbol):
        self.__populate_plugin_infos(symbol)
        template = self.engine.get_template('plugin.html')
        return template.render ({'symbol': symbol,
                                 'has_unique_feature': self.extension.has_unique_feature})

    def _format_pad_template_symbol(self, symbol):
        template = self.engine.get_template('padtemplate.html')
        return template.render ({'symbol': symbol})

    def _format_element_symbol(self, symbol):
        hierarchy = self._format_hierarchy(symbol)

        desc = ''
        if self.extension.has_unique_feature:
            desc = symbol.formatted_doc

        template = self.engine.get_template('element.html')
        return template.render({'symbol': symbol,
                                 'hierarchy': hierarchy,
                                 'desc': desc})

    def _format_enum(self, symbol):
        template = self.engine.get_template('enumtemplate.html')
        symbol.formatted_doc = template.render ({'values': symbol.values})
        return super()._format_enum(symbol)

    def _format_flags (self, flags):
        template = self.engine.get_template('gi_flags.html')
        out = template.render ({'flags': flags})
        return out

class GstExtension(Extension):
    extension_name = 'gst-extension'
    argument_prefix = 'gst'
    __dual_links = {} # Maps myelement:XXX to GstMyElement:XXX
    __parsed_cfiles = set()
    __caches = {} # cachefile -> dict
    __apps_sigs = set()
    __all_plugins_symbols = set()

    def __init__(self, app, project):
        super().__init__(app, project)
        self.__elements = {}
        self.__raw_comment_parser = GtkDocParser(project, section_file_matching=False)
        self.__plugins = None
        self.list_plugins_page = None
        # If we have a plugin with only one element, we render it on the plugin
        # page.
        self.has_unique_feature = False
        self.__on_index_symbols = []

    def __main_project_done_cb(self, app):
        with open(self.cache_file, 'w') as f:
            json.dump(self.cache, f, indent=4, sort_keys=True)

    def _make_formatter(self):
        return GstFormatter(self)

    def get_or_create_symbol(self, *args, **kwargs):
        sym = super().get_or_create_symbol(*args, **kwargs)
        if self.has_unique_feature:
            self.__on_index_symbols.append(sym)

        return sym

    def setup(self):
        stale_c, unlisted_c = self.get_stale_files(self.c_sources,
            'gst_c')

        # Make sure the cache file is save when the whole project
        # is done.
        if self.cache_file not in GstExtension.__apps_sigs and self.cache_file:
            GstExtension.__apps_sigs.add(self.cache_file)
            self.app.formatted_signal.connect(self.__main_project_done_cb)

        if not self.cache_file:
            if self.list_plugins_page:
                self.__plugins = self.get_or_create_symbol(
                        GstPluginsSymbol,
                        display_name="All " + self.project.project_name.replace("-", " ").title(),
                        unique_name=self.project.project_name + "-all-gst-plugins",
                        plugins=[], all_plugins=True)

                super().setup()
            return

        comment_parser = GtkDocParser(self.project, False)
        stale_c = set(stale_c) - GstExtension.__parsed_cfiles

        CCommentExtractor(self, comment_parser).parse_comments(stale_c)
        GstExtension.__parsed_cfiles.update(stale_c)

        if not self.cache:
            error('setup-issue', "No cache loaded or created for %s" % self.plugin)

        plugins = []
        if self.plugin:
            pname = self.plugin
            dot_idx = pname.rfind('.')
            if dot_idx > 0:
                pname = self.plugin[:dot_idx]
            if pname.startswith('libgst'):
                pname = pname[6:]
            elif pname.startswith('gst'):
                pname = pname[3:]
            try:
                plugin_node = {pname: self.cache[pname]}
            except KeyError:
                self.__main_project_done_cb(None) # Save the cache
                error('setup-issue', "Plugin %s not found" % pname)
        else:
            plugin_node = self.cache

        for libfile, plugin in plugin_node.items():
            plugins.append(self.__parse_plugin(libfile, plugin))

        if not self.plugin:
            self.__plugins = self.get_or_create_symbol(
                    GstPluginsSymbol,
                    display_name=self.project.project_name.replace("-", " ").title(),
                    unique_name=self.project.project_name + "-gst-plugins",
                    plugins=plugins)


        super().setup()

    def __update_tree_cb(self, tree, unlisted_sym_names):
        if self.list_plugins_page is None:
            index = tree.get_pages().get('gst-index')
            if index is None:
                return

            index.render_subpages = False

            index.symbol_names.add(self.__plugins.unique_name)
            for sym in self.__on_index_symbols:
                index.symbol_names.add(sym.unique_name)
        else:
            page = tree.get_pages().get(self.list_plugins_page)
            page.render_subpages = False
            page.extension_name = self.extension_name

            page.symbol_names.add(self.__plugins.unique_name)
            self.__plugins.plugins = self.__all_plugins_symbols

    def _get_smart_index_title(self):
        if self.plugin:
            return self.__plugins.display_name
        return 'GStreamer plugins documentation'

    @staticmethod
    def add_arguments(parser):
        group = parser.add_argument_group('Gst extension', DESCRIPTION)
        GstExtension.add_index_argument(group)
        # GstExtension.add_order_generated_subpages(group)
        GstExtension.add_sources_argument(group, prefix='gst-c')
        group.add_argument('--gst-cache-file', default=None)
        group.add_argument('--gst-list-plugins-page', default=None)
        group.add_argument('--gst-plugin-name', default=None)
        group.add_argument('--gst-plugins-path', default=None)

    def parse_config(self, config):
        self.c_sources = config.get_sources('gst_c')
        self.cache_file = config.get('gst_cache_file')
        self.plugin = config.get('gst_plugin_name')
        self.list_plugins_page = config.get('gst_list_plugins_page', None)
        paths = config.get('gst_plugins_path', '')
        info('Parsing config!')

        self.project.tree.update_signal.connect(self.__update_tree_cb)

        self.cache = {}
        if self.cache_file:
            self.cache = GstExtension.__caches.get(self.cache_file)
            if not self.cache:
                try:
                    with open(self.cache_file) as f:
                        self.cache = json.load(f)
                except FileNotFoundError:
                    pass

                if self.cache is None:
                    self.cache = {}
                GstExtension.__caches[self.cache_file] = self.cache

        super().parse_config(config)

    def _get_smart_key(self, symbol):
        if self.has_unique_feature:
            return None

        if isinstance(symbol, GstPluginSymbol):
            # PluginSymbol are rendered on the index page
            return None
        res = symbol.extra.get('gst-element-name')
        if res:
            res = res.replace("element-", "")

        return res

    def __create_hierarchy(self, element_dict):
        hierarchy = []

        for klass_name in element_dict["hierarchy"][1:]:
            link = Link(None, klass_name, klass_name)
            sym = QualifiedSymbol(type_tokens=[link])
            hierarchy.append(sym)

        hierarchy.reverse()
        return hierarchy

    def __type_tokens_from_type_name (self, type_name):
        res = [Link(None, type_name, type_name)]
        if type_name not in FUNDAMENTALS['python']:
            res.append('<span class="pointer-token">*</span>')
        return res

    def __create_signal_symbol(self, element, name, signal):
        atypes = signal['args']
        instance_type = element['hierarchy'][0]
        element_name = element['name']
        unique_name = "%s::%s" % (instance_type, name)
        aliases = ["%s::%s" % (element_name, name)]

        args_type_names = []
        args_type_names = [(self.__type_tokens_from_type_name('GstElement'), 'param_0')]
        for i in range(len(atypes)):
            args_type_names.append((self.__type_tokens_from_type_name(atypes[i]), 'param_%s' % (i + 1)))

        args_type_names.append((self.__type_tokens_from_type_name('gpointer'), "udata"))
        params = []

        for comment_name in [unique_name] + aliases:
            comment = self.app.database.get_comment(comment_name)
            if comment:
                for i, argname in enumerate(comment.params.keys()):
                    args_type_names[i] = (args_type_names[i][0], argname)

        for tokens, argname in args_type_names:
            params.append(ParameterSymbol (argname=argname, type_tokens=tokens))

        type_name = signal['retval']
        if type_name == 'void':
            retval = [None]
        else:
            tokens = self.__type_tokens_from_type_name(type_name)

            enum = signal.get('return-values')
            if enum:
                self.__create_enum_symbol(type_name, enum, element['name'])

            retval = [ReturnItemSymbol(type_tokens=tokens)]

        self.get_or_create_symbol(SignalSymbol,
            parameters=params, return_value=retval,
            display_name=name, unique_name=unique_name,
             extra={'gst-element-name': 'element-' + element_name},
            aliases=aliases, parent_name=element_name)

    def __create_signal_symbols(self, element):
        signals = element.get('signals', {})
        if not signals:
            return

        for name, signal in signals.items():
            self.__create_signal_symbol(element, name, signal)

    def __create_property_symbols(self, element):
        properties = element.get('properties', [])
        if not properties:
            return

        for name, prop in properties.items():
            unique_name = '%s:%s' % (element['hierarchy'][0], name)

            if name == "name":
                prop[CACHE_FIXED_DEFAULT] = True

            flags = [ReadableFlag()]
            if prop['writable']:
                flags += [WritableFlag()]
            if prop['construct-only']:
                flags += [ConstructOnlyFlag()]
            elif prop['construct']:
                flags += [ConstructFlag()]

            type_name = prop['type-name']

            tokens = self.__type_tokens_from_type_name(type_name)
            type_ = QualifiedSymbol(type_tokens=tokens)

            default = prop.get('default')
            enum = prop.get('values')
            if enum:
                type_ = self.__create_enum_symbol(prop['type-name'], enum, element['name'])

            aliases = ['%s:%s' % (element['name'], name)]
            res = self.app.database.get_symbol(unique_name)
            if res is None:
                res = self.get_or_create_symbol(
                    PropertySymbol,
                    prop_type=type_,
                    display_name=name, unique_name=unique_name,
                    aliases=aliases, parent_name=element['name'],
                    extra={'gst-element-name': 'element-' + element['name']},
                )

            if not self.app.database.get_comment(unique_name):
                comment = Comment(unique_name, Comment(name=name),
                    description=prop['blurb'])
                self.app.database.add_comment(comment)

            # FIXME This is incorrect, it's not yet format time (from gi_extension)
            extra_content = self.formatter._format_flags (flags)
            res.extension_contents['Flags'] = extra_content
            if default:
                res.extension_contents['Default value'] = default

    def __create_enum_symbol(self, type_name, enum, element_name):
        display_name = re.sub("([a-z])([A-Z])","\g<1>-\g<2>", type_name.strip('Gst'))
        symbol = self.get_or_create_symbol(
            NamedConstantsSymbols, anonymous=False,
            raw_text=None, display_name=display_name.capitalize(),
            unique_name=element_name + '_' + type_name, parent_name=element_name,
            extra={'gst-element-name': 'element-' + element_name})
        symbol.values = enum
        return symbol

    def __create_pad_template_symbols(self, element):
        templates = element.get('pad-templates', {})
        if not templates:
            return

        for tname, template in templates.items():
            name = tname.replace("%%", "%")
            unique_name = '%s->%s' % (element['hierarchy'][0], name)
            self.get_or_create_symbol(GstPadTemplateSymbol,
                name=name,
                direction=template["direction"],
                presence=template["presence"],
                caps=template["caps"], parent_name=element['name'],
                display_name=name, unique_name=unique_name,
                extra={'gst-element-name': 'element-' + element['name']})

    def __parse_plugin(self, plugin_name, plugin):
        elements = []
        if self.plugin and len(plugin.get('elements', {}).items()) == 1:
            self.has_unique_feature = True

        for ename, element in plugin.get('elements', {}).items():
            comment = None
            element['name'] = ename
            for comment_name in ['element-' + element['name'],
                                 element['name'], element['hierarchy'][0]]:
                comment = self.app.database.get_comment(comment_name)

            if not comment:
                comment = Comment(
                    'element-' + element['name'],
                    Comment(description=element['name']),
                    description=element['description'],
                    short_description=Comment(description=element['description']))
                self.app.database.add_comment(comment)
            elif not comment.short_description:
                comment.short_description = Comment(description=element['description'])

            sym = self.__elements[element['name']] = self.get_or_create_symbol(
                GstElementSymbol, display_name=element['name'],
                hierarchy=self.__create_hierarchy(element),
                unique_name=element['name'],
                filename=plugin_name,
                extra={'gst-element-name': 'element-' + element['name']},
                rank=str(element['rank']), author=element['author'],
                classification=element['klass'], plugin=plugin['filename'],
                aliases=['element-' + element['name'], element['hierarchy'][0]],
                package=plugin['package'])
            self.__create_property_symbols(element)
            self.__create_signal_symbols(element)
            self.__create_pad_template_symbols(element)
            elements.append(sym)

        plugin = self.get_or_create_symbol(
                GstPluginSymbol,
                description=plugin['description'],
                display_name=plugin_name,
                unique_name='plugin-' + plugin['filename'],
                license=plugin['license'],
                package=plugin['package'],
                filename=plugin['filename'],
                elements=elements,
                extra= {'gst-plugins': 'plugins-' + plugin['filename']})
        self.__all_plugins_symbols.add(plugin)

        if self.plugin:
            self.__plugins = plugin
        return plugin

    def __get_link_cb(self, resolver, name):
        link = self.__dual_links.get(name)
        if link:
            # Upsert link on the first run
            if isinstance(link, str):
                sym = self.app.database.get_symbol(link)
                link = sym.link

        if link:
            return link

        return (resolver, name)

    def format_page(self, page, link_resolver, output):
        link_resolver.get_link_signal.connect(GIExtension.search_online_links)
        super().format_page (page, link_resolver, output)
        link_resolver.get_link_signal.disconnect(GIExtension.search_online_links)

def get_extension_classes():
    return [GstExtension]
