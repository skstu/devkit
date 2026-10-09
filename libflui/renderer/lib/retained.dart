import 'dart:convert';

import 'package:flutter_svg/flutter_svg.dart';
import 'package:xml/xml.dart';

import 'localization.dart';
import 'emoji_picker.dart';
import 'emoji_text.dart';

import 'package:flutter_localizations/flutter_localizations.dart';

import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';

// Generic retained document protocol. Application state, prices, identifiers and
// actions are opaque strings. Only plot geometry uses double precision.
typedef RetainedAction = void Function(String name, String value);
const retainedTags = {
  'Window',
  'Control',
  'Container',
  'ScrollLayout',
  'WrapLayout',
  'VerticalLayout',
  'HorizontalLayout',
  'Label',
  'Button',
  'Icon',
  'Svg',
  'ResizeHandle',
  'EmojiPicker',
  'DecimalLabel',
  'DecimalButton',
  'Edit',
  'Combo',
  'Option',
  'TabLayout',
  'List',
  'ListHeader',
  'ListRow',
  'PaneCanvas',
  'Pane',
  'Plot',
};
const layoutKeys = {
  'visible',
  'width',
  'height',
  'weight',
  'padding',
  'inset',
  'childpadding',
  'bounds',
  'minimum',
};
const retainedAttributes = {
  'name',
  'text',
  'hint',
  'visible',
  'enabled',
  'width',
  'height',
  'weight',
  'padding',
  'inset',
  'childpadding',
  'fontsize',
  'font',
  'bold',
  'textcolor',
  'bkcolor',
  'bkcolor2',
  'hotbkcolor',
  'focusbkcolor',
  'defaultfontcolor',
  'disabledfontcolor',
  'disabledtextcolor',
  'nativebkcolor',
  'bordercolor',
  'bordersize',
  'topbordersize',
  'borderround',
  'clip',
  'cornerradius',
  'bevel',
  'gradientend',
  'align',
  'valign',
  'wordwrap',
  'endellipsis',
  'textpadding',
  'tooltip',
  'mouse',
  'windowdrag',
  'focusbordercolor',
  'maxchar',
  'password',
  'readonly',
  'multiline',
  'submitkey',
  'selectedid',
  'showheader',
  'vscrollbar',
  'hscrollbar',
  'itembkcolor',
  'itemselbkcolor',
  'itemhotbkcolor',
  'itemalign',
  'itemtextcolor',
  'columnlinecolor',
  'stripeheight',
  'stripecolor',
  'iconsize',
  'strokewidth',
  'emojiscale',
  'largepage',
  'normaltext',
  'normaltextkey',
  'largetext',
  'largetextkey',
  'glyph',
  'target',
  'svg',
  'keepalive',
  'dragable',
  'optionstyle',
  'cancelselected',
  'group',
  'selected',
  'live',
  'direction',
  'items_json',
  'virtual_count',
  'row_height',
  'header',
  'index',
  'scroll',
  'scrollanchor',
  'bounds',
  'minimum',
  'drag_height',
  'bars',
  'snapshot',
  'candles',
  'edit_revision',
  'select_all',
  'focus',
  'traverse',
  'locale',
  'fallback_locale',
  'translations',
  'textkey',
  'textargs',
  'hintkey',
  'hintargs',
  'tooltipkey',
  'tooltipargs',
  'safe_area',
  'theme',
  'theme_tokens',
  'theme_dark_tokens',
  'role',
  'tooltips',
  'dialog',
  'profile',
  'measure',
};
const retainedEvents = {
  'click',
  'enter',
  'valuechanged',
  'selchanged',
  'itemselect',
  'itemclick',
  'scroll',
  'navigate',
  'step',
  'pointchange',
  'resizestart',
  'resize',
  'resizeend',
  'textmeasure',
  'textwidth',
  'largeitemselect',
};

// Deliberately local, static vector graphics only: no scripts, fonts, images,
// network/file references, CSS or animation. Validation precedes atomic patches.
void validateInlineSvg(String source) {
  if (source.isEmpty) return;
  if (source.length > 1024 * 1024) throw const FormatException('SVG too large');
  final doc = XmlDocument.parse(source);
  const tags = {
    'svg',
    'g',
    'defs',
    'path',
    'rect',
    'circle',
    'ellipse',
    'line',
    'polyline',
    'polygon',
    'mask',
    'clipPath',
    'linearGradient',
    'radialGradient',
    'stop',
    'use',
    'title',
    'desc',
  };
  if (doc.rootElement.name.local != 'svg' ||
      doc.children.any((e) => e is XmlDoctype)) {
    throw const FormatException('Expected inline SVG');
  }
  for (final e in doc.descendants.whereType<XmlElement>()) {
    if (!tags.contains(e.name.local))
      throw const FormatException('Unsupported SVG element');
    for (final a in e.attributes) {
      if (a.name.local == 'style' ||
          a.name.local.startsWith('on') ||
          (a.name.local == 'href' &&
              !RegExp(r'^#[A-Za-z_][\w.-]*$').hasMatch(a.value)) ||
          (a.value.contains('url(') &&
              !RegExp(r'^url\(#[A-Za-z_][\w.-]*\)$').hasMatch(a.value))) {
        throw const FormatException(
          'SVG must contain only local static vectors',
        );
      }
    }
  }
}

class RNode extends ChangeNotifier {
  RNode(this.id, this.tag, this.attrs, this.children);
  final String id, tag;
  Map<String, String> attrs;
  List<RNode> children;
  double scrollOffset = 0;
  String text(String k, [String fallback = '']) => attrs[k] ?? fallback;
  double number(String k, [double fallback = 0]) =>
      double.tryParse(text(k)) ?? fallback;
  bool flag(String k, [bool fallback = false]) =>
      attrs.containsKey(k) ? text(k) == 'true' : fallback;
  void changed() => notifyListeners();
}

class RetainedModel {
  RetainedModel(this.action);
  final RetainedAction action;
  final root = ValueNotifier<RNode?>(null);
  Map<String, RNode> nodes = {};
  Map<String, String> parents = {};
  final revision = ValueNotifier<int>(0);
  final Map<String, _TreeViewState> editors = {};
  final emojiRecent = <String>[];
  final emojiLargeRecent = <String>[];

  bool interactive(RNode node) {
    var child = node;
    while (true) {
      if (!child.flag('visible', true) || !child.flag('enabled', true))
        return false;
      final parent = nodes[parents[child.id]];
      if (parent == null) return nodes[node.id] == node;
      if (parent.tag == 'TabLayout' &&
          parent.text('selectedid') != child.text('name'))
        return false;
      child = parent;
    }
  }

  _TreeViewState? emojiEditor(RNode picker) {
    final target = picker.text('target');
    final matches = nodes.values
        .where(
          (v) =>
              v.tag == 'Edit' && (v.id == target || v.text('name') == target),
        )
        .toList();
    if (target.isEmpty || matches.length != 1) return null;
    final node = matches.single;
    if (!interactive(picker) || !interactive(node) || node.flag('readonly'))
      return null;
    return editors[node.id];
  }

  bool insertEmoji(RNode picker, String emoji) {
    final state = emojiEditor(picker);
    if (state == null || !state.mounted) return false;
    final editor = state.editor!;
    final value = editor.value;
    final selection = value.selection.isValid
        ? value.selection
        : TextSelection.collapsed(offset: value.text.length);
    final start = selection.start.clamp(0, value.text.length);
    final end = selection.end.clamp(start, value.text.length);
    final text = value.text.replaceRange(start, end, emoji);
    if (text.characters.length > state.n.number('maxchar', 128).round())
      return false;
    editor.value = TextEditingValue(
      text: text,
      selection: TextSelection.collapsed(offset: start + emoji.length),
    );
    state.n.attrs['text'] = text;
    state.focus!.requestFocus();
    emit(state.n, 'valuechanged', text);
    emojiRecent.remove(emoji);
    emojiRecent.insert(0, emoji);
    if (emojiRecent.length > 10) emojiRecent.removeLast();
    emit(picker, 'itemselect', emoji);
    return true;
  }

  bool selectLargeEmoji(RNode picker, String emoji) {
    if (emojiEditor(picker) == null ||
        emoji.characters.length != 1 ||
        !isEmojiCluster(emoji))
      return false;
    emojiLargeRecent.remove(emoji);
    emojiLargeRecent.insert(0, emoji);
    if (emojiLargeRecent.length > 10) emojiLargeRecent.removeLast();
    emit(picker, 'largeitemselect', emoji);
    return true;
  }

  Map<String, String> attributes(Object? raw) {
    if (raw is! Map<String, dynamic>)
      throw const FormatException('attrs must be an object');
    final result = <String, String>{};
    for (final e in raw.entries) {
      if (e.value is! String ||
          !(retainedAttributes.contains(e.key) ||
              (e.key.startsWith('event_') &&
                  retainedEvents.contains(e.key.substring(6)))))
        throw FormatException('Invalid retained attribute: ${e.key}');
      final v = e.value as String;
      if ({
        'width',
        'height',
        'weight',
        'fontsize',
        'iconsize',
        'childpadding',
        'maxchar',
        'virtual_count',
        'row_height',
      }.contains(e.key)) {
        final n = double.tryParse(v);
        if (n == null || !n.isFinite || n < 0 || n > 100000)
          throw FormatException('Invalid ${e.key}');
      }
      if ({
            'visible',
            'enabled',
            'bold',
            'wordwrap',
            'live',
            'candles',
            'dialog',
            'profile',
            'tooltips',
          }.contains(e.key) &&
          v != 'true' &&
          v != 'false')
        throw FormatException('Invalid ${e.key}');
      if ({
        'items_json',
        'bars',
        'snapshot',
        'theme_tokens',
        'theme_dark_tokens',
      }.contains(e.key)) {
        final data = jsonDecode(v);
        if (e.key == 'items_json' &&
            (data is! List ||
                data.any((v) => v is! String) ||
                data.length > 10000))
          throw const FormatException('Invalid choices');
        if ((e.key == 'bars' || e.key == 'snapshot') &&
            (data is! List ||
                data.length > 10000 ||
                data.any(
                  (r) =>
                      r is! List ||
                      r.length != (e.key == 'bars' ? 5 : 2) ||
                      r.any((v) => v is! String),
                )))
          throw const FormatException('Invalid plot values');
        if ((e.key == 'theme_tokens' || e.key == 'theme_dark_tokens') &&
            (data is! Map || data.values.any((v) => v is! String)))
          throw const FormatException('Invalid theme tokens');
      }
      if (e.key == 'emojiscale') {
        final scale = double.tryParse(v);
        if (scale == null || !scale.isFinite || scale < 1 || scale > 3)
          throw const FormatException('Invalid emoji scale');
      }
      if (e.key == 'svg') validateInlineSvg(v);
      if (e.key == 'translations') UiStrings.validate(jsonDecode(v));
      if ({'textargs', 'hintargs', 'tooltipargs'}.contains(e.key)) {
        final data = jsonDecode(v);
        if (data is! Map || data.values.any((v) => v is! String))
          throw const FormatException('Invalid translation arguments');
      }
      result[e.key] = v;
    }
    return result;
  }

  void setTree(String json) {
    final next = <String, RNode>{}, newParents = <String, String>{};
    RNode parse(Object? data, int depth, String parent) {
      if (data is! Map<String, dynamic> ||
          data['id'] is! String ||
          data['tag'] is! String ||
          data['children'] is! List)
        throw const FormatException('Invalid retained node');
      final id = data['id'] as String, tag = data['tag'] as String;
      if (!RegExp(r'^[1-9][0-9]{0,19}$').hasMatch(id) ||
          !retainedTags.contains(tag) ||
          next.containsKey(id) ||
          next.length >= 16384 ||
          depth > 64)
        throw const FormatException('Invalid or duplicate retained node');
      final node = RNode(id, tag, attributes(data['attrs']), []);
      next[id] = node;
      if (parent.isNotEmpty) newParents[id] = parent;
      node.children = (data['children'] as List)
          .map((c) => parse(c, depth + 1, id))
          .toList();
      return node;
    }

    final tree = parse(jsonDecode(json), 0, '');
    if (tree.tag != 'Window')
      throw const FormatException('Expected Window root');
    // Validate everything before mutating the currently displayed document.
    final changed = <RNode>{};
    for (final entry in next.entries) {
      final old = nodes[entry.key];
      if (old != null && old.tag == entry.value.tag) {
        if (!mapEquals(old.attrs, entry.value.attrs)) changed.add(old);
        if (layoutKeys.any((k) => old.attrs[k] != entry.value.attrs[k])) {
          final parent = nodes[newParents[entry.key]];
          if (parent != null) changed.add(parent);
        }
        old.attrs = entry.value.attrs;
        next[entry.key] = old;
      }
    }
    void connect(Object? raw) {
      final data = raw as Map<String, dynamic>;
      final node = next[data['id']]!;
      final children = (data['children'] as List)
          .map((c) => next[c['id']]!)
          .toList();
      if (!listEquals(node.children, children)) changed.add(node);
      node.children = children;
      for (final child in data['children']) {
        connect(child);
      }
    }

    connect(jsonDecode(json));
    nodes = next;
    parents = newParents;
    root.value = next[tree.id];
    for (final node in changed) {
      node.changed();
    }
    revision.value++;
  }

  void patch(String json) {
    final list = jsonDecode(json);
    if (list is! List || list.length > 16384)
      throw const FormatException('Invalid patch');
    final updates = <String, Map<String, String>>{};
    for (final item in list) {
      if (item is! Map ||
          item['id'] is! String ||
          !nodes.containsKey(item['id']) ||
          updates.containsKey(item['id']))
        throw const FormatException('Unknown or duplicate patch ID');
      updates[item['id']] = attributes(item['attrs']);
    }
    final changed = <RNode>{};
    for (final entry in updates.entries) {
      final node = nodes[entry.key]!;
      if (mapEquals(node.attrs, entry.value)) continue;
      if (layoutKeys.any((k) => node.attrs[k] != entry.value[k])) {
        final parent = nodes[parents[node.id]];
        if (parent != null) changed.add(parent);
      }
      node.attrs = entry.value;
      changed.add(node);
    }
    for (final node in changed) {
      node.changed();
    }
    revision.value++;
  }

  void emit(RNode node, String event, [String value = '']) =>
      action('${node.id}:$event', value);
}

// Named defaults can be overridden by a Window's theme_tokens JSON. Layout and
// widget behavior do not depend on a theme; explicit XML styles take precedence.
class UiPalette {
  const UiPalette(this.tokens, [this.strings]);
  final UiStrings? strings;
  String text(RNode n, String field) =>
      strings?.field(n.attrs, field) ?? n.text(field);
  final Map<String, String> tokens;
  static UiPalette from(
    RNode root, {
    Brightness brightness = Brightness.light,
  }) {
    final name = root.text('theme', 'neutral');
    final values = <String, String>{
      'surface': '#f0f0f0',
      'panel': '#ffffff',
      'text': '#202020',
      'muted': '#777777',
      'accent': '#286b9b',
      'buttonTop': '#f6f6f6',
      'buttonBottom': '#d5d5d5',
      'border': '#909090',
      'radius': '2',
      'font': 'Arial',
    };
    if (name == 'xp-blue')
      values.addAll({
        'surface': '#ece9d8',
        'accent': '#245edb',
        'buttonTop': '#ffffff',
        'buttonBottom': '#e7e4d5',
        'border': '#7f9db9',
        'radius': '3',
        'font': 'Tahoma',
      });
    if (name == 'classic-2000' || name == 'classic-2003')
      values.addAll({
        'surface': '#d4d0c8',
        'accent': '#0a246a',
        'buttonTop': name == 'classic-2000' ? '#d4d0c8' : '#f1efe8',
        'buttonBottom': '#d4d0c8',
        'border': '#808080',
        'radius': '0',
        'font': 'Tahoma',
      });
    final dark =
        name == 'dark' || (name == 'system' && brightness == Brightness.dark);
    if (dark)
      values.addAll({
        'surface': '#191919',
        'panel': '#252525',
        'text': '#eeeeee',
        'muted': '#999999',
        'border': '#444444',
        'buttonTop': '#333333',
        'buttonBottom': '#333333',
      });
    values['brightness'] = dark ? 'dark' : 'light';
    values['classic'] = name == 'classic-2000' ? 'true' : 'false';
    if (root.attrs.containsKey('theme_tokens'))
      values.addAll(
        Map<String, String>.from(jsonDecode(root.text('theme_tokens')) as Map),
      );
    if (dark && root.attrs.containsKey('theme_dark_tokens'))
      values.addAll(
        Map<String, String>.from(
          jsonDecode(root.text('theme_dark_tokens')) as Map,
        ),
      );
    return UiPalette(
      values,
      UiStrings.from(
        root.attrs,
        WidgetsBinding.instance.platformDispatcher.locales,
      ),
    );
  }

  String resolve(String text) =>
      text.startsWith(r'$') ? tokens[text.substring(1)] ?? '' : text;
  Color color(String value, [Color fallback = Colors.transparent]) {
    value = resolve(value);
    if (RegExp(r'^#[0-9a-fA-F]{6}$').hasMatch(value))
      return Color(int.parse(value.substring(1), radix: 16) | 0xff000000);
    if (RegExp(r'^#[0-9a-fA-F]{8}$').hasMatch(value))
      return Color(int.parse(value.substring(1), radix: 16));
    return fallback;
  }
}

class RetainedApp extends StatefulWidget {
  const RetainedApp({super.key, required this.model});
  final RetainedModel model;
  @override
  State<RetainedApp> createState() => _RetainedAppState();
}

class _RetainedAppState extends State<RetainedApp> with WidgetsBindingObserver {
  RetainedModel get model => widget.model;
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeLocales(List<Locale>? locales) {
    if (mounted) setState(() {});
  }

  @override
  void didChangePlatformBrightness() {
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<RNode?>(
    valueListenable: model.root,
    builder: (_, root, _) => root == null
        ? const SizedBox.shrink()
        : ListenableBuilder(
            listenable: root,
            builder: (_, _) {
              final palette = UiPalette.from(
                root,
                brightness: WidgetsBinding
                    .instance
                    .platformDispatcher
                    .platformBrightness,
              );
              return MaterialApp(
                debugShowCheckedModeBanner: false,
                locale: palette.strings!.flutterLocale,
                supportedLocales: const [
                  Locale('en'),
                  Locale.fromSubtags(languageCode: 'zh', scriptCode: 'Hans'),
                  Locale.fromSubtags(languageCode: 'zh', scriptCode: 'Hant'),
                ],
                localizationsDelegates: GlobalMaterialLocalizations.delegates,
                theme: ThemeData(
                  useMaterial3: false,
                  brightness: palette.tokens['brightness'] == 'dark'
                      ? Brightness.dark
                      : Brightness.light,
                  fontFamily: root.text('font', palette.tokens['font']!),
                  visualDensity: VisualDensity.compact,
                  colorScheme: ColorScheme.fromSeed(
                    brightness: palette.tokens['brightness'] == 'dark'
                        ? Brightness.dark
                        : Brightness.light,
                    seedColor: palette.color(palette.tokens['accent']!),
                  ),
                ),
                home: Scaffold(
                  backgroundColor: palette.color(
                    root.text('bkcolor', palette.tokens['surface']!),
                  ),
                  body: SafeArea(
                    top: root.flag("safe_area"),
                    bottom: root.flag("safe_area"),
                    child: DefaultTextStyle(
                      style: TextStyle(
                        fontFamily: root.text('font', palette.tokens['font']!),
                        fontSize: root.number('fontsize', 13),
                        color: palette.color(
                          root.text(
                            'defaultfontcolor',
                            palette.tokens['text']!,
                          ),
                        ),
                        height: 1.1,
                      ),
                      child: CallbackShortcuts(
                        bindings: {
                          if (root.flag('dialog'))
                            const SingleActivator(
                              LogicalKeyboardKey.escape,
                            ): () =>
                                model.action('0:close', ''),
                        },
                        child: Focus(
                          autofocus: true,
                          child: TreeView(
                            node: root,
                            model: model,
                            palette: palette,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              );
            },
          ),
  );
}

EdgeInsets edges(String value) {
  final v = value
      .split(',')
      .map((s) => double.tryParse(s) ?? 0)
      .map((n) => n.clamp(0, 8192).toDouble())
      .toList();
  if (v.length == 4) return EdgeInsets.fromLTRB(v[0], v[1], v[2], v[3]);
  return EdgeInsets.all(v.firstOrNull ?? 0);
}

// Fixed-cell labels update their paragraph directly. Quote changes do not rebuild
// the surrounding widget tree or relayout a whole table. No price is parsed or
// rounded; semantics and text still use the original string.
class PlainLabel extends LeafRenderObjectWidget {
  const PlainLabel({
    super.key,
    required this.node,
    required this.style,
    required this.palette,
    required this.scaler,
    required this.fallbackColor,
  });
  final RNode node;
  final TextStyle style;
  final UiPalette palette;
  final TextScaler scaler;
  final Color fallbackColor;
  @override
  RenderObject createRenderObject(BuildContext context) =>
      PlainLabelRender(node, style, palette, scaler, fallbackColor);
  @override
  void updateRenderObject(BuildContext context, covariant PlainLabelRender r) =>
      r.update(node, style, palette, scaler, fallbackColor);
}

class PlainLabelRender extends RenderBox {
  PlainLabelRender(
    this.node,
    this.style,
    this.palette,
    this.scaler,
    this.fallbackColor,
  );
  RNode node;
  TextStyle style;
  UiPalette palette;
  TextScaler scaler;
  Color fallbackColor;
  final painter = TextPainter(textDirection: TextDirection.ltr, maxLines: 1);
  bool dirty = true;
  double lastWidth = -1;
  void update(
    RNode next,
    TextStyle font,
    UiPalette colors,
    TextScaler scale,
    Color fallback,
  ) {
    if (next != node) {
      if (attached) node.removeListener(changed);
      node = next;
      if (attached) node.addListener(changed);
    }
    style = font;
    palette = colors;
    scaler = scale;
    fallbackColor = fallback;
    changed();
  }

  @override
  void attach(PipelineOwner owner) {
    super.attach(owner);
    node.addListener(changed);
  }

  @override
  void detach() {
    node.removeListener(changed);
    super.detach();
  }

  @override
  void dispose() {
    painter.dispose();
    super.dispose();
  }

  void changed() {
    dirty = true;
    markNeedsSemanticsUpdate();
    if (hasSize && constraints.hasBoundedWidth && constraints.hasBoundedHeight)
      markNeedsPaint();
    else
      markNeedsLayout();
  }

  void prepare(double width) {
    if (!dirty && lastWidth == width) return;
    dirty = false;
    lastWidth = width;
    final textColor = palette.color(node.text('textcolor'), fallbackColor);
    final color = node.flag('enabled', true)
        ? textColor
        : palette.color(
            node.text('disabledtextcolor'),
            palette.color(node.text('textcolor'), const Color(0xff888888)),
          );
    painter.text = emojiTextSpan(
      palette.text(node, 'text'),
      style.copyWith(color: color),
      node.number('emojiscale', 1),
    );
    painter.textScaler = scaler;
    painter.ellipsis = node.flag('endellipsis') ? '…' : null;
    painter.layout(maxWidth: math.max(0, width));
  }

  @override
  Size computeDryLayout(BoxConstraints c) {
    final padding = edges(node.text('textpadding', '0'));
    prepare(
      c.hasBoundedWidth
          ? math.max(0, c.maxWidth - padding.horizontal)
          : double.infinity,
    );
    return c.constrain(
      Size(
        c.hasBoundedWidth ? c.maxWidth : painter.width + padding.horizontal,
        c.hasBoundedHeight ? c.maxHeight : painter.height + padding.vertical,
      ),
    );
  }

  @override
  void performLayout() {
    size = computeDryLayout(constraints);
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    final padding = edges(node.text('textpadding', '0'));
    final width = math.max(0.0, size.width - padding.horizontal),
        height = math.max(0.0, size.height - padding.vertical);
    prepare(width);
    final x = node.text('align') == 'center'
        ? (width - painter.width) / 2
        : node.text('align') == 'right'
        ? width - painter.width
        : 0.0;
    final y = node.text('valign') == 'top'
        ? 0.0
        : (height - painter.height) / 2;
    context.canvas.save();
    context.canvas.clipRect(offset & size);
    painter.paint(
      context.canvas,
      offset + Offset(padding.left + x, padding.top + y),
    );
    context.canvas.restore();
  }

  @override
  void describeSemanticsConfiguration(SemanticsConfiguration config) {
    super.describeSemanticsConfiguration(config);
    config.label = palette.text(node, 'text');
    config.textDirection = TextDirection.ltr;
  }
}

// Preserve the content at the visible bottom edge when a neighbouring pane
// changes viewport height. The correction is applied during layout, before paint.
// Content-only changes keep the usual scroll policy; this never forces a reader
// of history to the end of the list.
class VisibleEndScrollPhysics extends ScrollPhysics {
  const VisibleEndScrollPhysics({super.parent});
  @override
  VisibleEndScrollPhysics applyTo(ScrollPhysics? ancestor) =>
      VisibleEndScrollPhysics(parent: buildParent(ancestor));
  @override
  double adjustPositionForNewDimensions({
    required ScrollMetrics oldPosition,
    required ScrollMetrics newPosition,
    required bool isScrolling,
    required double velocity,
  }) {
    if (oldPosition.viewportDimension != newPosition.viewportDimension) {
      return (oldPosition.pixels +
              oldPosition.viewportDimension -
              newPosition.viewportDimension)
          .clamp(newPosition.minScrollExtent, newPosition.maxScrollExtent);
    }
    return super.adjustPositionForNewDimensions(
      oldPosition: oldPosition,
      newPosition: newPosition,
      isScrolling: isScrolling,
      velocity: velocity,
    );
  }
}

// Stroke icons opt in to an adjustable width in a 24-unit view box. Their hit
// targets and focus/tooltip behaviour remain those of the ordinary Icon control.
class OutlineIconPainter extends CustomPainter {
  const OutlineIconPainter(this.glyph, this.color, this.width);
  final String glyph;
  final Color color;
  final double width;
  @override
  void paint(Canvas canvas, Size size) {
    canvas.save();
    canvas.scale(size.width / 24, size.height / 24);
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = width.clamp(.5, 3)
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    if (glyph == 'chat') {
      canvas.drawPath(
        Path()
          ..moveTo(5, 3.5)
          ..lineTo(19, 3.5)
          ..quadraticBezierTo(21, 3.5, 21, 5.5)
          ..lineTo(21, 16)
          ..quadraticBezierTo(21, 18, 19, 18)
          ..lineTo(8, 18)
          ..lineTo(3, 21)
          ..lineTo(3, 5.5)
          ..quadraticBezierTo(3, 3.5, 5, 3.5)
          ..close(),
        paint,
      );
    } else if (glyph == 'discover') {
      canvas.drawCircle(const Offset(12, 12), 9.5, paint);
      canvas.drawPath(
        Path()
          ..moveTo(16.5, 7.5)
          ..lineTo(13.7, 13.7)
          ..lineTo(7.5, 16.5)
          ..lineTo(10.3, 10.3)
          ..close(),
        paint,
      );
    } else if (glyph == 'bluetooth') {
      canvas.drawPath(
        Path()
          ..moveTo(6, 7)
          ..lineTo(17, 17)
          ..lineTo(11, 22)
          ..lineTo(11, 2)
          ..lineTo(17, 7)
          ..lineTo(6, 17),
        paint,
      );
    } else if (glyph == 'lan') {
      canvas.drawRect(const Rect.fromLTWH(9, 2, 6, 5), paint);
      canvas.drawPath(
        Path()
          ..moveTo(12, 7)
          ..lineTo(12, 11)
          ..moveTo(5, 16)
          ..lineTo(5, 11)
          ..lineTo(19, 11)
          ..lineTo(19, 16)
          ..moveTo(12, 11)
          ..lineTo(12, 16),
        paint,
      );
      for (final x in [2.0, 9.0, 16.0]) {
        canvas.drawRect(Rect.fromLTWH(x, 16, 6, 5), paint);
      }
    } else if (glyph == 'globe') {
      canvas.drawCircle(const Offset(12, 12), 9, paint);
      canvas.drawOval(const Rect.fromLTWH(8, 3, 8, 18), paint);
      canvas.drawLine(const Offset(3, 12), const Offset(21, 12), paint);
      canvas.drawPath(
        Path()
          ..moveTo(5, 6.5)
          ..quadraticBezierTo(12, 9, 19, 6.5)
          ..moveTo(5, 17.5)
          ..quadraticBezierTo(12, 15, 19, 17.5),
        paint,
      );
    } else if (glyph == 'emoji') {
      canvas.drawCircle(const Offset(12, 12), 9, paint);
      final fill = Paint()..color = color;
      canvas.drawCircle(const Offset(8.5, 9), 1, fill);
      canvas.drawCircle(const Offset(15.5, 9), 1, fill);
      canvas.drawPath(
        Path()
          ..moveTo(7.5, 13.5)
          ..quadraticBezierTo(12, 19, 16.5, 13.5)
          ..close(),
        paint,
      );
    } else if (glyph == 'folder') {
      canvas.drawPath(
        Path()
          ..moveTo(3, 6)
          ..quadraticBezierTo(3, 4, 5, 4)
          ..lineTo(9, 4)
          ..lineTo(11, 7)
          ..lineTo(19, 7)
          ..quadraticBezierTo(21, 7, 21, 9)
          ..lineTo(21, 19)
          ..lineTo(3, 19)
          ..close(),
        paint,
      );
      canvas.drawLine(const Offset(3, 10), const Offset(21, 10), paint);
    } else {
      final path = Path();
      for (var tooth = 0; tooth < 8; tooth++) {
        for (var corner = 0; corner < 4; corner++) {
          final angle =
              (tooth * 45 + [-22.5, -10, 10, 22.5][corner]) * math.pi / 180;
          final radius = corner == 0 || corner == 3 ? 8.0 : 10.0;
          final point = Offset(
            12 + math.cos(angle) * radius,
            12 + math.sin(angle) * radius,
          );
          if (tooth == 0 && corner == 0) {
            path.moveTo(point.dx, point.dy);
          } else {
            path.lineTo(point.dx, point.dy);
          }
        }
      }
      canvas.drawPath(path..close(), paint);
      canvas.drawCircle(const Offset(12, 12), 3.4, paint);
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant OutlineIconPainter old) =>
      old.glyph != glyph || old.color != color || old.width != width;
}

class TreeView extends StatefulWidget {
  const TreeView({
    super.key,
    required this.node,
    required this.model,
    required this.palette,
    this.rowClick,
  });
  final RNode node;
  final RetainedModel model;
  final UiPalette palette;
  final void Function(int)? rowClick;
  @override
  State<TreeView> createState() => _TreeViewState();
}

class _TreeViewState extends State<TreeView> {
  RNode get n => widget.node;
  RetainedModel get m => widget.model;
  UiPalette get palette => widget.palette;
  TextEditingController? editor;
  FocusNode? focus;
  ScrollController? scroll;
  Map<String, String> previous = {};
  List<int> viewport = [];
  bool reportPending = false;
  String textMeasurement = '';
  bool hovered = false, focused = false;
  @override
  void initState() {
    super.initState();
    n.addListener(updated);
    previous = Map.of(n.attrs);
    if (n.tag == 'Edit') {
      editor = EmojiEditingController(
        text: n.text('text'),
        emojiScale: n.number('emojiscale', 1),
      );
      focus = FocusNode();
      focus!.addListener(focusChanged);
      m.editors[n.id] = this;
    }
    if (n.tag == 'Combo') focus = FocusNode();
    if (n.tag == 'List') {
      scroll = ScrollController(initialScrollOffset: n.scrollOffset);
      scroll!.addListener(() => n.scrollOffset = scroll!.offset);
      scroll!.addListener(reportViewport);
    }
  }

  @override
  void didUpdateWidget(TreeView old) {
    super.didUpdateWidget(old);
    if (old.node != n) {
      old.node.removeListener(updated);
      n.addListener(updated);
    }
    sync();
  }

  void focusChanged() {
    if (!(focus?.hasFocus ?? false)) sync();
  }

  void sync() {
    if (editor != null &&
        editor!.text != n.text('text') &&
        (!(focus?.hasFocus ?? false) ||
            previous['edit_revision'] != n.attrs['edit_revision'])) {
      final value = n.text('text');
      editor!.value = TextEditingValue(
        text: value,
        selection: TextSelection.collapsed(offset: value.length),
      );
    }
    if (focus != null &&
        n.attrs['focus'] != null &&
        previous['focus'] != n.attrs['focus'])
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) focus!.requestFocus();
      });
    if (editor != null &&
        n.attrs['select_all'] != null &&
        previous['select_all'] != n.attrs['select_all'])
      editor!.selection = TextSelection(
        baseOffset: 0,
        extentOffset: editor!.text.length,
      );
    if (n.attrs['traverse'] != null &&
        previous['traverse'] != n.attrs['traverse'])
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) {
          if (n.text('traverse').startsWith('backward'))
            FocusScope.of(context).previousFocus();
          else
            FocusScope.of(context).nextFocus();
        }
      });
    if (scroll != null && previous['scroll'] != n.attrs['scroll'])
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && scroll!.hasClients)
          scroll!.jumpTo(
            n.number('scroll').clamp(0, scroll!.position.maxScrollExtent),
          );
      });
    previous = Map.of(n.attrs);
  }

  void updated() {
    if (!mounted) return;
    if (n.tag == 'Label' &&
        !n.flag('wordwrap') &&
        {...previous.keys, ...n.attrs.keys}.every(
          (k) => previous[k] == n.attrs[k] || k == 'text' || k == 'textcolor',
        )) {
      previous = Map.of(n.attrs);
      return;
    }
    setState(sync);
  }

  @override
  void dispose() {
    n.removeListener(updated);
    if (m.editors[n.id] == this) m.editors.remove(n.id);
    focus?.removeListener(focusChanged);
    focus?.dispose();
    editor?.dispose();
    scroll?.dispose();
    super.dispose();
  }

  void emit(String event, [String value = '']) => m.emit(n, event, value);
  void reportViewport() {
    if (reportPending) return;
    reportPending = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      reportPending = false;
      if (!mounted || !scroll!.hasClients) return;
      final row = n.number('row_height', 24).clamp(1, 8192);
      final offset = scroll!.offset;
      n.scrollOffset = offset;
      final v = [
        math.max(0, (offset / row).floor() - 1),
        (scroll!.position.viewportDimension / row).ceil() + 3,
        offset.round(),
      ];
      if (!listEquals(viewport, v)) {
        viewport = v;
        emit('viewport', v.join(','));
      }
    });
  }

  Widget child(RNode c, {void Function(int)? rowClick}) => TreeView(
    key: ValueKey(c.id),
    node: c,
    model: m,
    palette: palette,
    rowClick: rowClick ?? widget.rowClick,
  );
  Color color(String key, [Color fallback = Colors.transparent]) =>
      palette.color(n.text(key), fallback);
  Widget textContent(BuildContext context, {bool decimal = false}) {
    final inherited = DefaultTextStyle.of(context).style;
    final style = inherited.copyWith(
      fontFamily: n.attrs['font'],
      fontSize: n.attrs.containsKey('fontsize') ? n.number('fontsize') : null,
      fontWeight: n.flag('bold') ? FontWeight.bold : FontWeight.normal,
      color: n.flag('enabled', true)
          ? color('textcolor', inherited.color ?? Colors.black)
          : color(
              'disabledtextcolor',
              color('textcolor', const Color(0xff888888)),
            ),
    );
    if (n.tag == 'Label' && !n.flag('wordwrap'))
      return PlainLabel(
        node: n,
        style: style,
        palette: palette,
        scaler: MediaQuery.textScalerOf(context),
        fallbackColor: inherited.color ?? Colors.black,
      );
    final text = palette.text(n, 'text'),
        align = n.text('align') == 'center'
            ? TextAlign.center
            : n.text('align') == 'right'
            ? TextAlign.right
            : TextAlign.left;
    Widget result;
    if (decimal && RegExp(r'^-?[0-9,]+\.[0-9]{2,}$').hasMatch(text)) {
      final end = text.length,
          before = text.substring(0, math.max(0, end - 3)),
          big = text.substring(end - 3, end - 1),
          tail = text.substring(end - 1);
      result = Text.rich(
        TextSpan(
          children: [
            TextSpan(
              text: before,
              style: style.copyWith(fontSize: (style.fontSize ?? 18) * .82),
            ),
            TextSpan(
              text: big,
              style: style.copyWith(
                fontSize: (style.fontSize ?? 18) * 1.24,
                fontWeight: FontWeight.bold,
              ),
            ),
            WidgetSpan(
              alignment: PlaceholderAlignment.top,
              child: Text(
                tail,
                style: style.copyWith(fontSize: (style.fontSize ?? 18) * .64),
              ),
            ),
          ],
        ),
        textAlign: align,
        maxLines: 1,
        overflow: TextOverflow.clip,
      );
    } else {
      final span = emojiTextSpan(text, style, n.number('emojiscale', 1));
      Widget render() => Text.rich(
        span,
        textAlign: align,
        maxLines: n.flag('wordwrap') ? null : 1,
        overflow: n.flag('endellipsis')
            ? TextOverflow.ellipsis
            : TextOverflow.clip,
      );
      result = (n.flag('event_textmeasure') || n.flag('event_textwidth'))
          ? LayoutBuilder(
              builder: (_, constraints) {
                final painter = TextPainter(
                  text: span,
                  textDirection: TextDirection.ltr,
                  textScaler: MediaQuery.textScalerOf(context),
                  maxLines: n.flag('wordwrap') ? null : 1,
                )..layout(maxWidth: constraints.maxWidth);
                final measurement =
                    '${constraints.maxWidth.floor()},${painter.height.ceil()}';
                final intrinsicWidth = painter.maxIntrinsicWidth.ceil();
                painter.dispose();
                final signature =
                    '$text\u0000${style.fontFamily},${style.fontSize},${n.number('emojiscale', 1)},$measurement,$intrinsicWidth,${n.flag('event_textmeasure')},${n.flag('event_textwidth')}';
                if (textMeasurement != signature) {
                  textMeasurement = signature;
                  WidgetsBinding.instance.addPostFrameCallback((_) {
                    if (mounted && textMeasurement == signature) {
                      if (n.flag('event_textmeasure'))
                        emit('textmeasure', measurement);
                      if (n.flag('event_textwidth'))
                        emit('textwidth', '$intrinsicWidth');
                    }
                  });
                }
                return render();
              },
            )
          : render();
    }
    final horizontal = n.text('align') == 'center'
        ? 0.0
        : n.text('align') == 'right'
        ? 1.0
        : -1.0;
    result = Align(
      alignment: Alignment(horizontal, n.text('valign') == 'top' ? -1 : 0),
      child: result,
    );
    if (decimal && n.flag('live') && n.number('direction') != 0)
      result = Stack(
        fit: StackFit.expand,
        children: [
          result,
          Positioned(
            top: 0,
            right: 1,
            child: Text(
              n.number('direction') > 0 ? '↑' : '↓',
              style: TextStyle(
                color: n.number('direction') > 0
                    ? const Color(0xff00aa32)
                    : const Color(0xffcc5555),
                fontSize: 13,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),
        ],
      );
    return Padding(padding: edges(n.text('textpadding', '0')), child: result);
  }

  Widget flex(bool vertical) {
    final items = n.children.where((c) => c.flag('visible', true)).toList();
    final children = <Widget>[];
    for (var i = 0; i < items.length; i++) {
      if (i > 0 && n.number('childpadding') > 0)
        children.add(
          SizedBox(
            width: vertical ? 0 : n.number('childpadding'),
            height: vertical ? n.number('childpadding') : 0,
          ),
        );
      final c = items[i];
      Widget view = child(
        c,
        rowClick: n.tag == 'ListRow'
            ? (column) => widget.rowClick?.call(column)
            : null,
      );
      if (n.tag == 'ListRow' && widget.rowClick != null)
        view = GestureDetector(
          behavior: HitTestBehavior.translucent,
          onTap: () => widget.rowClick!(i),
          child: view,
        );
      final extent = c.number(vertical ? 'height' : 'width');
      if (extent <= 0)
        view = Expanded(
          flex: math.max(1, c.number('weight', 1).round()),
          child: view,
        );
      children.add(view);
    }
    return Flex(
      direction: vertical ? Axis.vertical : Axis.horizontal,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: children,
    );
  }

  Widget list() {
    final headerId = n.text('header');
    RNode? header;
    final rows = <RNode>[];
    for (final c in n.children) {
      if (c.id == headerId)
        header = c;
      else
        rows.add(c);
    }
    final count = n.number('virtual_count').round(), virtual = count > 0;
    final indices = {
      for (final r in rows) int.tryParse(r.text('index')) ?? -1: r,
    };
    Widget row(RNode r, int index) {
      final background = n.number('selected', -1).round() == index
          ? color('itemselbkcolor', const Color(0xffd8e8ef))
          : index.isEven
          ? color('stripecolor', color('itembkcolor'))
          : color('itembkcolor');
      return RepaintBoundary(
        child: ColoredBox(
          color: background,
          child: child(
            r,
            rowClick: (column) {
              emit('itemclick', '$index,$column');
            },
          ),
        ),
      );
    }

    final view = LayoutBuilder(
      builder: (_, constraints) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (mounted) reportViewport();
        });
        return ListView.builder(
          controller: scroll,
          physics: n.text('scrollanchor') == 'bottom'
              ? const VisibleEndScrollPhysics()
              : null,
          itemExtent: virtual ? n.number('row_height', 24) : null,
          itemCount: virtual ? count : rows.length,
          scrollCacheExtent: const ScrollCacheExtent.pixels(0),
          itemBuilder: (_, i) {
            final r = virtual ? indices[i] : rows[i];
            if (r == null) return const SizedBox.shrink();
            return SizedBox(
              height: virtual
                  ? n.number('row_height', 24)
                  : r.number('height', 24),
              child: row(r, i),
            );
          },
        );
      },
    );
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (header != null && n.flag('showheader', true))
          SizedBox(height: header.number('height', 28), child: child(header)),
        Expanded(
          child: Scrollbar(
            controller: scroll,
            thumbVisibility: n.flag('vscrollbar'),
            child: view,
          ),
        ),
      ],
    );
  }

  Widget input() {
    (editor as EmojiEditingController).emojiScale = n.number('emojiscale', 1);
    Widget field = TextField(
      controller: editor,
      focusNode: focus,
      enabled: n.flag('enabled', true),
      readOnly: n.flag('readonly'),
      obscureText: n.flag('password'),
      maxLength: n.number('maxchar', 128).round(),
      maxLines: n.flag('multiline') ? null : 1,
      textAlign: n.text('align') == 'center'
          ? TextAlign.center
          : TextAlign.left,
      style: DefaultTextStyle.of(context).style.copyWith(
        fontSize: n.number('fontsize', 13),
        color: color(
          'textcolor',
          DefaultTextStyle.of(context).style.color ?? Colors.black,
        ),
      ),
      textAlignVertical: switch (n.text('valign')) {
        'top' => TextAlignVertical.top,
        'bottom' => TextAlignVertical.bottom,
        _ => TextAlignVertical.center,
      },
      decoration: InputDecoration(
        isDense: true,
        contentPadding: const EdgeInsets.symmetric(horizontal: 3, vertical: 0),
        border: InputBorder.none,
        enabledBorder: InputBorder.none,
        focusedBorder: InputBorder.none,
        disabledBorder: InputBorder.none,
        hintText: palette.text(n, 'hint'),
        counterText: '',
      ),
      onSubmitted: (_) {
        // Configured multiline editors submit only through the hardware key
        // handler. A software keyboard's newline/done must not send a draft.
        if (n.flag('event_enter') &&
            (!n.flag('multiline') ||
                !{'enter', 'ctrl-enter'}.contains(n.text('submitkey'))))
          emit('enter');
      },
      onChanged: (v) => emit('valuechanged', v),
    );
    return Focus(
      onKeyEvent: (_, event) {
        final submitKey = n.text('submitkey');
        if (n.flag('multiline') &&
            (submitKey == 'enter' || submitKey == 'ctrl-enter') &&
            n.flag('event_enter') &&
            m.interactive(n) &&
            !n.flag('readonly') &&
            (event is KeyDownEvent || event is KeyRepeatEvent) &&
            (event.logicalKey == LogicalKeyboardKey.enter ||
                event.logicalKey == LogicalKeyboardKey.numpadEnter)) {
          final value = editor!.value;
          // Leave candidate confirmation to the IME; never submit composing
          // text. Control is literal on every platform, including macOS.
          if (value.composing.isValid && !value.composing.isCollapsed)
            return KeyEventResult.ignored;
          final keyboard = HardwareKeyboard.instance;
          if (keyboard.isAltPressed || keyboard.isMetaPressed)
            return KeyEventResult.ignored;
          final submit =
              !keyboard.isShiftPressed &&
              keyboard.isControlPressed == (submitKey == 'ctrl-enter');
          if (submit) {
            // Consume repeats too, but admit at most one event per key press.
            if (event is KeyDownEvent) emit('enter');
          } else {
            final selection = value.selection.isValid
                ? value.selection
                : TextSelection.collapsed(offset: value.text.length);
            final start = selection.start.clamp(0, value.text.length);
            final end = selection.end.clamp(start, value.text.length);
            final text = value.text.replaceRange(start, end, '\n');
            if (text.characters.length <= n.number('maxchar', 128).round()) {
              editor!.value = TextEditingValue(
                text: text,
                selection: TextSelection.collapsed(offset: start + 1),
              );
              n.attrs['text'] = text;
              emit('valuechanged', text);
            }
          }
          return KeyEventResult.handled;
        }
        if (event is! KeyDownEvent) return KeyEventResult.ignored;
        if (event.logicalKey == LogicalKeyboardKey.tab &&
            n.flag('event_navigate')) {
          emit(
            'navigate',
            HardwareKeyboard.instance.isShiftPressed ? 'backward' : 'forward',
          );
          return KeyEventResult.handled;
        }
        if (n.flag('event_step') &&
            (event.logicalKey == LogicalKeyboardKey.arrowUp ||
                event.logicalKey == LogicalKeyboardKey.arrowDown)) {
          emit(
            'step',
            event.logicalKey == LogicalKeyboardKey.arrowUp ? 'up' : 'down',
          );
          return KeyEventResult.handled;
        }
        return KeyEventResult.ignored;
      },
      child: field,
    );
  }

  Widget combo() {
    final items = (jsonDecode(n.text('items_json', '[]')) as List)
        .cast<String>();
    final selected = n.number('selected', -1).round();
    return Focus(
      onKeyEvent: (_, event) {
        if (event is KeyDownEvent &&
            event.logicalKey == LogicalKeyboardKey.tab &&
            n.flag('event_navigate')) {
          emit(
            'navigate',
            HardwareKeyboard.instance.isShiftPressed ? 'backward' : 'forward',
          );
          return KeyEventResult.handled;
        }
        return KeyEventResult.ignored;
      },
      child: DropdownButtonHideUnderline(
        child: DropdownButton<int>(
          focusNode: focus,
          dropdownColor: palette.color(r'$panel'),
          iconEnabledColor: palette.color(r'$text'),
          isExpanded: true,
          isDense: true,
          value: selected >= 0 && selected < items.length ? selected : null,
          items: [
            for (var i = 0; i < items.length; i++)
              DropdownMenuItem(
                value: i,
                child: Text(
                  items[i],
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 12,
                    color: color('textcolor', palette.color(r'$text')),
                  ),
                ),
              ),
          ],
          onChanged: n.flag('enabled', true)
              ? (v) {
                  if (v != null) emit('itemselect', v.toString());
                }
              : null,
        ),
      ),
    );
  }

  Widget icon() {
    if (n.number('strokewidth') > 0 &&
        {
          'chat',
          'discover',
          'settings',
          'emoji',
          'folder',
          'bluetooth',
          'lan',
          'globe',
        }.contains(n.text('glyph'))) {
      final size = n.number('iconsize', 16);
      return Center(
        child: CustomPaint(
          size: Size.square(size),
          painter: OutlineIconPainter(
            n.text('glyph'),
            color('textcolor', const Color(0xff666666)),
            n.number('strokewidth'),
          ),
        ),
      );
    }
    const icons = {
      'chat': Icons.chat_bubble_outline,
      'devices': Icons.devices_outlined,
      'discover': Icons.explore_outlined,
      'bluetooth': Icons.bluetooth,
      'lan': Icons.lan_outlined,
      'globe': Icons.public,
      'settings': Icons.settings_outlined,
      'keyboard': Icons.keyboard_outlined,
      'search': Icons.search,
      'emoji': Icons.sentiment_satisfied_alt,
      'folder': Icons.folder_outlined,
      'back': Icons.chevron_left,
      'more': Icons.more_horiz,
      'person': Icons.person_outline,
      'info': Icons.info_outline,
      'chart': Icons.bar_chart,
      'launch': Icons.open_in_new,
      'ticket': Icons.content_copy,
      'add': Icons.add,
      'remove': Icons.remove,
      'favorite': Icons.star,
      'star': Icons.star,
      'check': Icons.check,
      'close': Icons.close,
      'candlestick': Icons.candlestick_chart,
      'circle': Icons.circle,
      'status': Icons.circle,
      'warning': Icons.info_outline,
    };
    return Center(
      child: Icon(
        icons[n.text('glyph')] ?? Icons.circle,
        size: n.number('iconsize', 16),
        color: color('textcolor', const Color(0xff666666)),
      ),
    );
  }

  Widget decorate(Widget result) {
    final themedButton = n.text('role') == 'button';
    final interactive = m.interactive(n) && n.flag('mouse', true);
    final activeBackground =
        interactive && focused && n.text('focusbkcolor').isNotEmpty
        ? n.text('focusbkcolor')
        : interactive && hovered && n.text('hotbkcolor').isNotEmpty
        ? n.text('hotbkcolor')
        : '';
    final top = activeBackground.isNotEmpty
        ? palette.color(activeBackground)
        : color(
            'bkcolor',
            themedButton ? palette.color(r'$buttonTop') : Colors.transparent,
          );
    final bottom = activeBackground.isNotEmpty
        ? top
        : color(
            'bkcolor2',
            themedButton && !n.attrs.containsKey('bkcolor')
                ? palette.color(r'$buttonBottom')
                : top,
          );
    final border = edges(n.text('bordersize', themedButton ? '1' : '0'));
    final borderColor = color(
      'bordercolor',
      themedButton ? palette.color(r'$border') : const Color(0xff909090),
    );
    final radius =
        double.tryParse(palette.resolve(n.text('borderround'))) ??
        (n.text('cornerradius').isNotEmpty
            ? double.tryParse(n.text('cornerradius').split(',').first) ?? 0
            : themedButton
            ? double.tryParse(palette.tokens['radius'] ?? '0') ?? 0
            : 0);
    final decoration = BoxDecoration(
      color: top,
      gradient: top != bottom
          ? LinearGradient(
              begin: Alignment.topCenter,
              end: Alignment.bottomCenter,
              colors: [top, bottom],
            )
          : null,
      border: Border(
        left: border.left > 0
            ? BorderSide(color: borderColor, width: border.left)
            : BorderSide.none,
        top: border.top > 0
            ? BorderSide(color: borderColor, width: border.top)
            : BorderSide.none,
        right: border.right > 0
            ? BorderSide(color: borderColor, width: border.right)
            : BorderSide.none,
        bottom: border.bottom > 0
            ? BorderSide(color: borderColor, width: border.bottom)
            : BorderSide.none,
      ),
      borderRadius:
          border.isNonNegative &&
              border.left == border.top &&
              border.top == border.right &&
              border.right == border.bottom
          ? BorderRadius.circular(radius)
          : null,
    );
    result = DecoratedBox(
      decoration: decoration,
      child: Padding(padding: edges(n.text('inset', '0')), child: result),
    );
    final classic = palette.tokens['classic'] == 'true';
    if (n.flag('bevel') ||
        (classic && (themedButton || n.text('role') == 'inset')))
      result = CustomPaint(
        foregroundPainter: BevelPainter(
          sunken: classic && n.text('role') == 'inset',
        ),
        child: result,
      );
    if (n.flag('clip'))
      result = ClipRRect(
        borderRadius: BorderRadius.circular(radius),
        child: result,
      );
    if (palette.text(n, 'tooltip').isNotEmpty &&
        m.root.value?.flag('tooltips', true) != false)
      result = Tooltip(
        message: palette.text(n, 'tooltip'),
        waitDuration: const Duration(milliseconds: 600),
        child: result,
      );
    final width = n.number('width'), height = n.number('height');
    if (width > 0 || height > 0)
      result = SizedBox(
        width: width > 0 ? width : null,
        height: height > 0 ? height : null,
        child: result,
      );
    result = Padding(padding: edges(n.text('padding', '0')), child: result);
    return result;
  }

  @override
  Widget build(BuildContext context) {
    if (!m.interactive(n)) {
      hovered = false;
      focused = false;
    }
    if (!n.flag('visible', true)) return const SizedBox.shrink();
    Widget result;
    switch (n.tag) {
      case 'Window':
      case 'Container':
        result = Stack(
          fit: StackFit.expand,
          children: n.children
              .where((c) => c.flag('visible', true))
              .map(child)
              .toList(),
        );
      case 'ScrollLayout':
        result = SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              for (final c in n.children.where((c) => c.flag('visible', true)))
                SizedBox(height: c.number('height', 40), child: child(c)),
            ],
          ),
        );
      case 'WrapLayout':
        result = Wrap(
          spacing: n.number('childpadding'),
          runSpacing: n.number('childpadding'),
          children: [
            for (final c in n.children.where((c) => c.flag('visible', true)))
              SizedBox(
                width: c.number('width', 100),
                height: c.number('height', 34),
                child: child(c),
              ),
          ],
        );
      case 'VerticalLayout':
      case 'Pane':
        result = flex(true);
      case 'HorizontalLayout':
      case 'ListHeader':
      case 'ListRow':
        result = flex(false);
      case 'TabLayout':
        final selected = n.text('selectedid');
        final c = n.children
            .where((c) => c.text('name') == selected)
            .firstOrNull;
        result = n.flag('keepalive')
            ? IndexedStack(
                index: c == null ? null : n.children.indexOf(c),
                sizing: StackFit.expand,
                children: [
                  for (final page in n.children)
                    ExcludeFocus(
                      key: ValueKey(page.id),
                      excluding: page != c,
                      child: TickerMode(enabled: page == c, child: child(page)),
                    ),
                ],
              )
            : c == null
            ? const SizedBox.shrink()
            : child(c);
      case 'Control':
        result = const SizedBox.expand();
      case 'Label':
      case 'Button':
      case 'DecimalLabel':
      case 'DecimalButton':
        result = textContent(context, decimal: n.tag.startsWith('Decimal'));
      case 'Icon':
        result = icon();
      case 'EmojiPicker':
        result = EmojiPickerSurface(
          revision: m.revision,
          valid: () => m.emojiEditor(n) != null,
          editorRevision: () => m.emojiEditor(n)?.n.text('edit_revision') ?? '',
          recent: m.emojiRecent,
          largePage: n.flag('largepage'),
          normalLabel: palette.text(n, 'normaltext').isEmpty
              ? 'Normal'
              : palette.text(n, 'normaltext'),
          largeLabel: palette.text(n, 'largetext').isEmpty
              ? 'Large'
              : palette.text(n, 'largetext'),
          largeRecent: m.emojiLargeRecent,
          onLargeSelect: (emoji) => m.selectLargeEmoji(n, emoji),
          items: (jsonDecode(n.text('items_json', '[]')) as List)
              .cast<String>(),
          label: palette.text(n, 'tooltip'),
          recentLabel: palette.text(n, 'hint'),
          allLabel: palette.text(n, 'text'),
          background: palette.color(r'$panel'),
          foreground: palette.color(r'$text'),
          muted: palette.color(r'$muted'),
          border: palette.color(r'$border'),
          accent: palette.color(r'$accent'),
          radius:
              double.tryParse(
                palette.tokens[n
                        .text('borderround', r'$radius')
                        .replaceFirst(r'$', '')] ??
                    n.text('borderround', '8'),
              ) ??
              8,
          onSelect: (emoji) => m.insertEmoji(n, emoji),
          restoreFocus: () => m.emojiEditor(n)?.focus?.requestFocus(),
          child: icon(),
        );
      case 'ResizeHandle':
        result = TextFieldTapRegion(
          child: ResizeHandleSurface(
            horizontal: n.text('direction') == 'horizontal',
            enabled: n.flag('enabled', true),
            label: palette.text(n, 'tooltip'),
            color: color('textcolor', palette.color(r'$accent')),
            emit: emit,
          ),
        );
      case 'Svg':
        result = n.text('svg').isEmpty
            ? const SizedBox.shrink()
            : SvgPicture.string(
                n.text('svg'),
                fit: BoxFit.contain,
                colorFilter: n.text('textcolor').isEmpty
                    ? null
                    : ColorFilter.mode(color('textcolor'), BlendMode.srcIn),
                excludeFromSemantics: true,
              );
      case 'Edit':
        result = input();
      case 'Combo':
        result = combo();
      case 'Option':
        result = Row(
          children: [
            SizedBox(
              width: 22,
              child: Checkbox(
                materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                value: n.flag('selected'),
                onChanged: n.flag('enabled', true)
                    ? (_) => emit('click')
                    : null,
              ),
            ),
            const SizedBox(width: 4),
            Expanded(child: textContent(context)),
          ],
        );
      case 'List':
        result = list();
      case 'PaneCanvas':
        result = PaneSurface(node: n, model: m, palette: palette);
      case 'Plot':
        void point(Offset position) {
          final bars = jsonDecode(n.text('bars', '[]')) as List;
          final box = context.findRenderObject() as RenderBox?;
          if (bars.isNotEmpty && box != null)
            emit(
              'pointchange',
              ((position.dx / box.size.width * bars.length).floor().clamp(
                0,
                bars.length - 1,
              )).toString(),
            );
        }
        result = MouseRegion(
          onHover: (e) => point(e.localPosition),
          child: GestureDetector(
            onTapDown: (e) => point(e.localPosition),
            child: CustomPaint(
              painter: PlotPainter(n.attrs, palette),
              size: Size.infinite,
            ),
          ),
        );
      default:
        throw StateError('Unvalidated retained tag');
    }
    final clickable =
        n.flag('event_click') || {'Button', 'DecimalButton'}.contains(n.tag);
    final stateBackground =
        clickable &&
        n.flag('mouse', true) &&
        (n.text('hotbkcolor').isNotEmpty || n.text('focusbkcolor').isNotEmpty);
    // Opt-in state backgrounds include the whole decorated row in its hit area,
    // including inset and border. Existing controls keep their original wrappers.
    if (stateBackground) result = decorate(result);
    if (clickable && n.flag('mouse', true))
      result = FocusableActionDetector(
        enabled: n.flag('enabled', true),
        onFocusChange: (value) {
          if (focused == value || !mounted) return;
          focused = value;
          if (n.text('focusbkcolor').isNotEmpty) setState(() {});
        },
        mouseCursor: n.flag('enabled', true)
            ? SystemMouseCursors.click
            : SystemMouseCursors.basic,
        shortcuts: const {
          SingleActivator(LogicalKeyboardKey.enter): ActivateIntent(),
          SingleActivator(LogicalKeyboardKey.space): ActivateIntent(),
        },
        actions: {
          ActivateIntent: CallbackAction<ActivateIntent>(
            onInvoke: (_) {
              if (n.flag('enabled', true)) emit('click');
              return null;
            },
          ),
        },
        child: Semantics(
          button: true,
          label: palette.text(n, 'tooltip').isNotEmpty
              ? palette.text(n, 'tooltip')
              : palette.text(n, 'text'),
          enabled: n.flag('enabled', true),
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: n.flag('enabled', true) ? () => emit('click') : null,
            child: result,
          ),
        ),
      );
    if (stateBackground) {
      void updateHover(bool value) {
        if (hovered == value || !mounted) return;
        hovered = value;
        if (n.text('hotbkcolor').isNotEmpty) setState(() {});
      }

      result = MouseRegion(
        onEnter: (_) => updateHover(true),
        onExit: (_) => updateHover(false),
        child: result,
      );
    }
    if (!n.flag('enabled', true))
      result = ExcludeFocus(child: IgnorePointer(child: result));
    if (!stateBackground) result = decorate(result);
    if ({
          'Window',
          'PaneCanvas',
          'List',
          'Container',
          'VerticalLayout',
          'HorizontalLayout',
          'Pane',
          'TabLayout',
        }.contains(n.tag) ||
        n.flag('measure'))
      result = Measure(
        onSize: (rect) {
          emit(
            'layout',
            '${rect.left.round()},${rect.top.round()},${rect.width.round()},${rect.height.round()}',
          );
        },
        child: result,
      );
    if ({
      'Pane',
      'ListRow',
      'Label',
      'DecimalLabel',
      'DecimalButton',
    }.contains(n.tag))
      result = RepaintBoundary(child: result);
    return result;
  }
}

// Reports cumulative window-space displacement, so consumer-driven layout
// changes cannot move the origin or double-count a drag. No sizing policy lives
// here: the consumer owns the adjacent panes and their limits.
class ResizeHandleSurface extends StatefulWidget {
  const ResizeHandleSurface({
    super.key,
    required this.horizontal,
    required this.enabled,
    required this.label,
    required this.color,
    required this.emit,
  });
  final bool horizontal, enabled;
  final String label;
  final Color color;
  final void Function(String, String) emit;
  @override
  State<ResizeHandleSurface> createState() => _ResizeHandleSurfaceState();
}

class _ResizeHandleSurfaceState extends State<ResizeHandleSurface> {
  Offset? origin;
  int displacement = 0;
  bool highlighted = false;
  void start(DragStartDetails d) {
    origin = d.globalPosition;
    displacement = 0;
    widget.emit('resizestart', '0');
  }

  void move(DragUpdateDetails d) {
    if (origin == null) return;
    final delta = d.globalPosition - origin!;
    displacement = (widget.horizontal ? delta.dx : delta.dy).round();
    widget.emit('resize', '$displacement');
  }

  void end() {
    if (origin == null) return;
    origin = null;
    widget.emit('resizeend', '$displacement');
  }

  void step(int delta) {
    if (!widget.enabled || origin != null) return;
    widget.emit('resizestart', '0');
    widget.emit('resize', '$delta');
    widget.emit('resizeend', '$delta');
  }

  @override
  Widget build(BuildContext context) => MouseRegion(
    cursor: widget.horizontal
        ? SystemMouseCursors.resizeLeftRight
        : SystemMouseCursors.resizeUpDown,
    child: Focus(
      canRequestFocus: widget.enabled,
      onFocusChange: (value) => setState(() => highlighted = value),
      onKeyEvent: (_, event) {
        if (!widget.enabled || event is! KeyDownEvent)
          return KeyEventResult.ignored;
        final key = event.logicalKey;
        final negative = widget.horizontal
            ? LogicalKeyboardKey.arrowLeft
            : LogicalKeyboardKey.arrowUp;
        final positive = widget.horizontal
            ? LogicalKeyboardKey.arrowRight
            : LogicalKeyboardKey.arrowDown;
        if (key != negative && key != positive) return KeyEventResult.ignored;
        step(key == negative ? -8 : 8);
        return KeyEventResult.handled;
      },
      child: Semantics(
        label: widget.label,
        enabled: widget.enabled,
        onIncrease: widget.enabled ? () => step(8) : null,
        onDecrease: widget.enabled ? () => step(-8) : null,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onVerticalDragStart: widget.enabled && !widget.horizontal
              ? start
              : null,
          onVerticalDragUpdate: widget.enabled && !widget.horizontal
              ? move
              : null,
          onVerticalDragEnd: widget.enabled && !widget.horizontal
              ? (_) => end()
              : null,
          onVerticalDragCancel: widget.enabled && !widget.horizontal
              ? end
              : null,
          onHorizontalDragStart: widget.enabled && widget.horizontal
              ? start
              : null,
          onHorizontalDragUpdate: widget.enabled && widget.horizontal
              ? move
              : null,
          onHorizontalDragEnd: widget.enabled && widget.horizontal
              ? (_) => end()
              : null,
          onHorizontalDragCancel: widget.enabled && widget.horizontal
              ? end
              : null,
          child: ColoredBox(
            color: highlighted
                ? widget.color.withValues(alpha: .35)
                : Colors.transparent,
            child: const SizedBox.expand(),
          ),
        ),
      ),
    ),
  );
}

class Measure extends SingleChildRenderObjectWidget {
  const Measure({super.key, required this.onSize, required super.child});
  final void Function(Rect) onSize;
  @override
  RenderObject createRenderObject(BuildContext c) => MeasureRender(onSize);
  @override
  void updateRenderObject(BuildContext c, covariant MeasureRender r) {
    r.onSize = onSize;
  }
}

class MeasureRender extends RenderProxyBox {
  MeasureRender(this.onSize);
  void Function(Rect) onSize;
  Rect? last;
  @override
  void performLayout() {
    super.performLayout();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!attached || !hasSize) return;
      final r = localToGlobal(Offset.zero) & size;
      if (last != r) {
        last = r;
        onSize(r);
      }
    });
  }
}

class PaneSurface extends StatefulWidget {
  const PaneSurface({
    super.key,
    required this.node,
    required this.model,
    required this.palette,
  });
  final RNode node;
  final RetainedModel model;
  final UiPalette palette;
  @override
  State<PaneSurface> createState() => _PaneSurfaceState();
}

class _PaneSurfaceState extends State<PaneSurface> {
  final drafts = <String, Rect>{};
  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (_, limits) => Stack(
      children: [
        for (final pane in widget.node.children.where(
          (c) => c.flag('visible', true),
        ))
          ...buildPane(pane, limits),
      ],
    ),
  );
  List<Widget> buildPane(RNode p, BoxConstraints limits) {
    final parts = p
        .text('bounds', '0,0,300,200')
        .split(',')
        .map((s) => double.tryParse(s) ?? 0)
        .toList();
    if (parts.length != 4) return [];
    final rect =
        drafts[p.id] ??
        Rect.fromLTWH(
          parts[0],
          parts[1],
          math.max(1, parts[2]),
          math.max(1, parts[3]),
        );
    final min = p
        .text('minimum', '160,120')
        .split(',')
        .map((s) => double.tryParse(s) ?? 0)
        .toList();
    void change(DragUpdateDetails d, bool resize) {
      setState(() {
        drafts[p.id] = resize
            ? Rect.fromLTWH(
                rect.left,
                rect.top,
                (rect.width + d.delta.dx).clamp(
                  min[0],
                  math.max(min[0], limits.maxWidth - rect.left),
                ),
                (rect.height + d.delta.dy).clamp(
                  min.last,
                  math.max(min.last, limits.maxHeight - rect.top),
                ),
              )
            : Rect.fromLTWH(
                (rect.left + d.delta.dx).clamp(
                  0,
                  math.max(0, limits.maxWidth - 60),
                ),
                (rect.top + d.delta.dy).clamp(
                  0,
                  math.max(0, limits.maxHeight - 30),
                ),
                rect.width,
                rect.height,
              );
      });
    }

    void end(DragEndDetails _) {
      final r = drafts.remove(p.id);
      if (r != null)
        widget.model.emit(
          widget.node,
          'pane',
          '${p.text('name')}:${r.left.round()},${r.top.round()},${r.width.round()},${r.height.round()}',
        );
    }

    bool headerDrag = false;
    return [
      Positioned.fromRect(
        rect: rect,
        child: GestureDetector(
          onPanStart: (d) {
            headerDrag =
                d.localPosition.dy <= widget.node.number('drag_height', 28);
          },
          onPanUpdate: (d) {
            if (headerDrag) change(d, false);
          },
          onPanEnd: (d) {
            if (headerDrag) end(d);
          },
          child: ClipRect(
            child: TreeView(
              key: ValueKey(p.id),
              node: p,
              model: widget.model,
              palette: widget.palette,
            ),
          ),
        ),
      ),
      Positioned(
        left: rect.right - 12,
        top: rect.bottom - 12,
        width: 12,
        height: 12,
        child: MouseRegion(
          cursor: SystemMouseCursors.resizeUpLeftDownRight,
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onPanUpdate: (d) => change(d, true),
            onPanEnd: end,
            child: const Icon(
              Icons.drag_handle,
              size: 12,
              color: Color(0xff888888),
            ),
          ),
        ),
      ),
    ];
  }
}

class BevelPainter extends CustomPainter {
  BevelPainter({this.sunken = false});
  final bool sunken;
  @override
  void paint(Canvas canvas, Size s) {
    final p = Paint()
      ..strokeWidth = 1
      ..color = sunken
          ? const Color(0xff808080)
          : Colors.white.withValues(alpha: .8);
    canvas.drawLine(const Offset(.5, .5), Offset(s.width - .5, .5), p);
    canvas.drawLine(const Offset(.5, .5), Offset(.5, s.height - .5), p);
    p.color = sunken ? Colors.white : Colors.black.withValues(alpha: .5);
    canvas.drawLine(
      Offset(.5, s.height - .5),
      Offset(s.width - .5, s.height - .5),
      p,
    );
    canvas.drawLine(
      Offset(s.width - .5, .5),
      Offset(s.width - .5, s.height - .5),
      p,
    );
  }

  @override
  bool shouldRepaint(BevelPainter old) => old.sunken != sunken;
}

class PlotPainter extends CustomPainter {
  PlotPainter(this.attrs, this.palette);
  final Map<String, String> attrs;
  final UiPalette palette;
  void label(Canvas c, String s, Offset p) {
    final t = TextPainter(
      text: TextSpan(
        text: s,
        style: const TextStyle(fontSize: 11, color: Color(0xff666666)),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    t.paint(c, p);
  }

  @override
  void paint(Canvas canvas, Size size) {
    final bars = jsonDecode(attrs['bars'] ?? '[]') as List;
    final snap = jsonDecode(attrs['snapshot'] ?? '[]') as List;
    if (bars.isEmpty) {
      for (var i = 0; i < snap.length; i++) {
        label(canvas, '${snap[i][0]}  ${snap[i][1]}', Offset(24, 24 + i * 32));
      }
      return;
    }
    final values = <List<double>>[];
    for (final bar in bars) {
      final v = (bar as List)
          .skip(1)
          .map((s) => double.tryParse(s as String))
          .toList();
      if (v.any((n) => n == null || !n.isFinite)) return;
      values.add(v.cast<double>());
    }
    final low = values.expand((v) => v).reduce(math.min),
        high = values.expand((v) => v).reduce(math.max);
    final range = math.max(1e-12, high - low),
        height = math.max(1.0, size.height - 40),
        width = math.max(1.0, size.width - 72),
        step = width / values.length;
    double y(double v) => 20 + (high - v) / range * height;
    final grid = Paint()
      ..color = const Color(0xffdddddd)
      ..strokeWidth = 1;
    for (var i = 0; i < 5; i++) {
      final yy = 20 + height * i / 4;
      canvas.drawLine(Offset(0, yy), Offset(width, yy), grid);
      label(
        canvas,
        (high - range * i / 4).toStringAsFixed(4),
        Offset(width + 4, yy - 6),
      );
    }
    final path = Path();
    for (var i = 0; i < values.length; i++) {
      final v = values[i], x = (i + .5) * step;
      if (attrs['candles'] != 'false') {
        final p = Paint()
          ..color = v[3] >= v[0]
              ? const Color(0xff198e64)
              : const Color(0xffcc5555);
        canvas.drawLine(Offset(x, y(v[1])), Offset(x, y(v[2])), p);
        canvas.drawRect(
          Rect.fromLTRB(
            x - step * .3,
            y(math.max(v[0], v[3])),
            x + step * .3,
            y(math.min(v[0], v[3])) + 1,
          ),
          p,
        );
      } else {
        if (i == 0)
          path.moveTo(x, y(v[3]));
        else
          path.lineTo(x, y(v[3]));
      }
    }
    if (attrs['candles'] == 'false')
      canvas.drawPath(
        path,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.4
          ..color = const Color(0xff286b9b),
      );
  }

  @override
  bool shouldRepaint(PlotPainter old) => !mapEquals(attrs, old.attrs);
}
