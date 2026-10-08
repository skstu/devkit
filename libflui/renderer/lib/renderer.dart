import 'dart:convert';

import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:xml/xml.dart';

typedef ActionSink = void Function(String name, String value);
const equality = DeepCollectionEquality();
const schemas = <String, Set<String>>{
  'Window': {},
  'VerticalLayout': {'gap', 'padding'},
  'HorizontalLayout': {'gap', 'padding'},
  'Label': {'text', 'fontsize', 'bold'},
  'Edit': {'value', 'action', 'enabled', 'hint'},
  'Button': {'text', 'action', 'enabled'},
  'List': {'items', 'action', 'selected'},
};
const common = {'name', 'width', 'height', 'weight'};

class Node {
  Node(this.tag, this.attrs, this.children);
  final String tag;
  final Map<String, String> attrs;
  final List<Node> children;
}

Node parse(String xml) {
  if (xml.contains(RegExp(r'<!\s*(DOCTYPE|ENTITY)', caseSensitive: false))) {
    throw const FormatException('DTD and entities are not supported');
  }
  XmlDocument document;
  try {
    document = XmlDocument.parse(xml);
  } on XmlException catch (e) {
    throw FormatException(e.toString());
  }
  final names = <String>{};
  var count = 0;
  Node visit(XmlElement el, int depth, String parent) {
    if (++count > 4096 || depth > 32)
      throw const FormatException('XML tree limit exceeded');
    final tag = el.name.qualified;
    final schema = schemas[tag];
    if (schema == null) throw FormatException('Unknown control: $tag');
    if (el.children.whereType<XmlText>().any(
      (t) => t.value.trim().isNotEmpty,
    )) {
      throw const FormatException('Use the text attribute for labels');
    }
    final attrs = <String, String>{};
    for (final a in el.attributes) {
      final key = a.name.qualified;
      if (!schema.contains(key) && !common.contains(key))
        throw FormatException('Unknown $tag attribute: $key');
      attrs[key] = a.value;
      if ({
        'width',
        'height',
        'gap',
        'padding',
        'fontsize',
        'weight',
      }.contains(key)) {
        final number = double.tryParse(a.value);
        if (number == null ||
            !number.isFinite ||
            number < 0 ||
            number > 8192 ||
            (key == 'weight' &&
                (number < 1 || number.roundToDouble() != number))) {
          throw FormatException('Invalid $key');
        }
      }
      if (key == 'enabled' || key == 'bold') {
        if (!isBinding(a.value) && a.value != 'true' && a.value != 'false')
          throw FormatException('Invalid $key');
      }
    }
    if (attrs.containsKey('weight') &&
        parent != 'VerticalLayout' &&
        parent != 'HorizontalLayout') {
      throw const FormatException('weight requires a layout parent');
    }
    final name = attrs['name'];
    if (name != null && (name.isEmpty || !names.add(name)))
      throw const FormatException('Duplicate or empty control name');
    final children = el.childElements
        .map((e) => visit(e, depth + 1, tag))
        .toList();
    if ((tag == 'Window' || tag == 'List') && children.length != 1)
      throw FormatException('$tag requires one child');
    if ({'Label', 'Edit', 'Button'}.contains(tag) && children.isNotEmpty)
      throw FormatException('$tag cannot contain children');
    if (tag == 'Window' && depth != 0)
      throw const FormatException('Window must be the root');
    if (tag == 'List' &&
        (!attrs.containsKey('height') || !isBinding(attrs['items'] ?? ''))) {
      throw const FormatException('List requires a height and items binding');
    }
    return Node(tag, attrs, children);
  }

  final root = visit(document.rootElement, 0, '');
  if (root.tag != 'Window') throw const FormatException('Root must be Window');
  return root;
}

bool isBinding(String value) =>
    RegExp(r'^\{[A-Za-z_][A-Za-z0-9_.]*\}$').hasMatch(value);
Object? resolve(
  String? text,
  Map<String, dynamic> state,
  Map<String, dynamic>? item,
) {
  if (text == null || !isBinding(text)) return text;
  final path = text.substring(1, text.length - 1).split('.');
  Object? result = path.first == 'item' ? item : state;
  for (final part in path.first == 'item' ? path.skip(1) : path) {
    result = result is Map ? result[part] : null;
  }
  return result;
}

void validate(
  Node node,
  Map<String, dynamic> state, [
  Map<String, dynamic>? item,
]) {
  for (final entry in node.attrs.entries) {
    final value = resolve(entry.value, state, item);
    if (value == null) continue;
    if (entry.key == 'items') continue;
    if ({'enabled', 'bold'}.contains(entry.key) &&
        isBinding(entry.value) &&
        value is! bool) {
      throw FormatException('${entry.key} must bind a boolean');
    }
    if ({'text', 'value', 'selected', 'hint'}.contains(entry.key) &&
        value is! String) {
      throw FormatException(
        '${entry.key} must bind a string (preserve exact decimals/IDs)',
      );
    }
  }
  if (node.tag == 'List') {
    final rows = resolve(node.attrs['items'], state, item);
    if (rows == null) return;
    if (rows is! List || rows.length > 10000)
      throw const FormatException('Invalid list');
    final ids = <String>{};
    for (final row in rows) {
      if (row is! Map<String, dynamic> ||
          row['id'] is! String ||
          (row['id'] as String).isEmpty ||
          !ids.add(row['id'] as String)) {
        throw const FormatException('Rows require unique nonempty string IDs');
      }
      validate(node.children.single, state, row);
    }
  } else {
    for (final child in node.children) {
      validate(child, state, item);
    }
  }
}

class UiModel extends ChangeNotifier {
  UiModel(this.action);
  final ActionSink action;
  final document = ValueNotifier<Node?>(null);
  Map<String, dynamic> state = {};
  void load(String xml) {
    final next = parse(xml);
    validate(next, state);
    document.value = next;
  }

  void setStateJson(String json) {
    final next = jsonDecode(json);
    if (next is! Map<String, dynamic>)
      throw const FormatException('State must be a JSON object');
    if (document.value != null) validate(document.value!, next);
    if (equality.equals(state, next)) return;
    state = next;
    notifyListeners();
  }

  @override
  void dispose() {
    document.dispose();
    super.dispose();
  }
}

class FluiApp extends StatelessWidget {
  const FluiApp({super.key, required this.model});
  final UiModel model;
  @override
  Widget build(BuildContext context) => MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: ThemeData(
      colorSchemeSeed: const Color(0xff265d77),
      useMaterial3: true,
    ),
    home: Scaffold(
      body: SafeArea(
        child: ValueListenableBuilder<Node?>(
          valueListenable: model.document,
          builder: (_, root, _) => root == null
              ? const SizedBox.shrink()
              : NodeView(key: ObjectKey(root), node: root, model: model),
        ),
      ),
    ),
  );
}

class NodeView extends StatefulWidget {
  const NodeView({
    super.key,
    required this.node,
    required this.model,
    this.item,
  });
  final Node node;
  final UiModel model;
  final Map<String, dynamic>? item;
  @override
  State<NodeView> createState() => _NodeViewState();
}

class _NodeViewState extends State<NodeView> {
  late Map<String, Object?> values;
  TextEditingController? editor;
  FocusNode? editorFocus;
  Map<String, Object?> read() => widget.node.attrs.map(
    (k, v) => MapEntry(k, resolve(v, widget.model.state, widget.item)),
  );
  @override
  void initState() {
    super.initState();
    values = read();
    if (widget.node.tag == 'Edit') {
      editor = TextEditingController(text: text('value'));
      editorFocus = FocusNode()..addListener(syncEditor);
    }
    widget.model.addListener(updated);
  }

  @override
  void didUpdateWidget(NodeView old) {
    super.didUpdateWidget(old);
    if (old.model != widget.model) {
      old.model.removeListener(updated);
      widget.model.addListener(updated);
    }
    values = read();
    syncEditor();
  }

  void syncEditor() {
    final controller = editor;
    if (editorFocus?.hasFocus == true) return;
    if (controller != null && controller.text != text('value')) {
      final value = text('value');
      controller.value = TextEditingValue(
        text: value,
        selection: TextSelection.collapsed(offset: value.length),
      );
    }
  }

  void updated() {
    final next = read();
    if (equality.equals(values, next)) return;
    setState(() {
      values = next;
      syncEditor();
    });
  }

  String text(String key) => values[key]?.toString() ?? '';
  double? number(String key) => double.tryParse(text(key));
  bool flag(String key, bool fallback) => values[key] == null
      ? fallback
      : values[key] == true || values[key] == 'true';
  void action(String value) {
    final name = text('action');
    if (name.isNotEmpty) widget.model.action(name, value);
  }

  @override
  void dispose() {
    widget.model.removeListener(updated);
    editorFocus?.dispose();
    editor?.dispose();
    super.dispose();
  }

  Widget child(Node node, [Map<String, dynamic>? item]) => NodeView(
    key: node.attrs['name'] == null
        ? ObjectKey(node)
        : ValueKey(node.attrs['name']),
    node: node,
    model: widget.model,
    item: item ?? widget.item,
  );
  @override
  Widget build(BuildContext context) {
    final node = widget.node;
    Widget result;
    switch (node.tag) {
      case 'Window':
        result = child(node.children.single);
      case 'VerticalLayout':
      case 'HorizontalLayout':
        final vertical = node.tag == 'VerticalLayout';
        final children = <Widget>[];
        for (final element in node.children) {
          if (children.isNotEmpty)
            children.add(
              SizedBox(
                width: vertical ? 0 : number('gap') ?? 0,
                height: vertical ? number('gap') ?? 0 : 0,
              ),
            );
          Widget current = child(element);
          if (element.attrs.containsKey('weight'))
            current = Expanded(
              flex: int.parse(element.attrs['weight']!),
              child: current,
            );
          children.add(current);
        }
        result = Padding(
          padding: EdgeInsets.all(number('padding') ?? 0),
          child: vertical
              ? Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  mainAxisSize: MainAxisSize.min,
                  children: children,
                )
              : Row(children: children),
        );
      case 'Label':
        result = Text(
          text('text'),
          style: TextStyle(
            fontSize: number('fontsize'),
            fontWeight: flag('bold', false)
                ? FontWeight.bold
                : FontWeight.normal,
          ),
        );
      case 'Button':
        result = FilledButton(
          onPressed: flag('enabled', true) ? () => action('') : null,
          child: Text(text('text')),
        );
      case 'Edit':
        result = TextField(
          controller: editor,
          focusNode: editorFocus,
          enabled: flag('enabled', true),
          maxLength: 128,
          decoration: InputDecoration(
            border: const OutlineInputBorder(),
            hintText: text('hint'),
            counterText: '',
          ),
          onChanged: action,
        );
      case 'List':
        final rows = values['items'] as List? ?? const [];
        result = ListView.builder(
          itemCount: rows.length,
          itemExtent: 44,
          itemBuilder: (_, index) {
            final row = rows[index] as Map<String, dynamic>;
            return Material(
              key: ValueKey(row['id']),
              color: row['id'] == values['selected']
                  ? const Color(0xffe0edf4)
                  : Colors.transparent,
              child: InkWell(
                onTap: () => action(row['id'] as String),
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  child: child(node.children.single, row),
                ),
              ),
            );
          },
        );
      default:
        throw StateError('Unvalidated control');
    }
    if (number('width') != null || number('height') != null)
      result = SizedBox(
        width: number('width'),
        height: number('height'),
        child: result,
      );
    return result;
  }
}
