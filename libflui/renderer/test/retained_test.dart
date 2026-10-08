import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> node(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};
void main() {
  test(
    'tree and patch rejection is atomic, identities survive replacement',
    () {
      final model = RetainedModel((_, _) {});
      model.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'Label', {'text': '1.10000'}),
          ]),
        ),
      );
      final label = model.nodes['2'];
      expect(
        () => model.patch(
          jsonEncode([
            {
              'id': '2',
              'attrs': {'text': 'lost'},
            },
            {'id': '3', 'attrs': {}},
          ]),
        ),
        throwsFormatException,
      );
      expect(label!.text('text'), '1.10000');
      expect(
        () => model.setTree(
          jsonEncode(
            node('1', 'Window', {}, [
              node('2', 'Label', {}),
              node('2', 'Label', {}),
            ]),
          ),
        ),
        throwsFormatException,
      );
      expect(model.nodes['2'], same(label));
      model.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'Label', {'text': '1.10001'}),
          ]),
        ),
      );
      expect(model.nodes['2'], same(label));
      expect(label.text('text'), '1.10001');
    },
  );
  testWidgets(
    'full replacement invalidates parent bounds without replacing controls',
    (tester) async {
      final model = RetainedModel((_, _) {});
      Map<String, Object> tree(String bounds) => node('1', 'Window', {}, [
        node('2', 'PaneCanvas', {}, [
          node(
            '3',
            'Pane',
            {'name': 'pane', 'bounds': bounds},
            [
              node('4', 'Label', {'text': 'panel'}),
            ],
          ),
        ]),
      ]);
      model.setTree(jsonEncode(tree('0,0,300,200')));
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pump();
      expect(
        tester.getSize(find.byKey(const ValueKey('3'))),
        const Size(300, 200),
      );
      model.setTree(jsonEncode(tree('20,40,700,400')));
      await tester.pump();
      expect(
        tester.getSize(find.byKey(const ValueKey('3'))),
        const Size(700, 400),
      );
      expect(
        tester.getTopLeft(find.byKey(const ValueKey('3'))),
        const Offset(20, 40),
      );
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'quote patches retain editor and focus, precise value, enter is not submit',
    (tester) async {
      final actions = <String>[];
      final model = RetainedModel((n, v) => actions.add('$n=$v'));
      model.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Label', {'text': '1.10000', 'height': '30'}),
              node('4', 'Edit', {
                'text': '1',
                'height': '30',
                'event_enter': 'true',
              }),
              node('5', 'Button', {
                'text': 'submit',
                'height': '30',
                'event_click': 'true',
              }),
              node('6', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pump();
      final input = find.byType(TextField);
      await tester.tap(input);
      await tester.enterText(input, '0.125');
      final state = tester.widget<TextField>(input);
      final controller = state.controller;
      controller!.selection = const TextSelection.collapsed(offset: 2);
      for (var i = 0; i < 300; i++) {
        model.patch(
          jsonEncode([
            {
              'id': '3',
              'attrs': {
                'text': i.isEven ? '1.10001' : '1.10000',
                'height': '30',
              },
            },
          ]),
        );
        await tester.pump(const Duration(milliseconds: 16));
      }
      expect(tester.widget<TextField>(input).controller, same(controller));
      expect(controller.text, '0.125');
      expect(controller.selection.baseOffset, 2);
      expect(state.focusNode!.hasFocus, isTrue);
      // Ordinary stale state echoes cannot overwrite an active edit.
      model.patch(
        jsonEncode([
          {
            'id': '4',
            'attrs': {'text': '1', 'height': '30', 'event_enter': 'true'},
          },
        ]),
      );
      await tester.pump();
      expect(controller.text, '0.125');
      await tester.testTextInput.receiveAction(TextInputAction.done);
      await tester.pump();
      expect(actions.where((e) => e.startsWith('5:click')), isEmpty);
      expect(actions, contains('4:enter='));
      await tester.tap(find.text('submit'));
      await tester.pump();
      expect(actions.where((e) => e == '5:click=').length, 1);
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'virtual list reports bounded visible range for ten thousand rows',
    (tester) async {
      final events = <String>[];
      final model = RetainedModel((n, v) {
        if (n == '2:viewport') events.add(v);
      });
      model.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node(
              '2',
              'List',
              {
                'virtual_count': '10000',
                'row_height': '24',
                'vscrollbar': 'true',
              },
              [
                node(
                  '3',
                  'ListRow',
                  {'index': '0', 'height': '24'},
                  [
                    node('4', 'Label', {'text': 'first'}),
                  ],
                ),
              ],
            ),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pump();
      await tester.pump();
      expect(events, isNotEmpty);
      final range = events.last.split(',').map(int.parse).toList();
      expect(range[1], lessThan(40));
      expect(
        find.byWidgetPredicate(
          (w) => w is PlainLabel && w.node.text('text') == 'first',
        ),
        findsOneWidget,
      );
      expect(model.nodes.length, 4);
      await tester.drag(find.byType(ListView), const Offset(0, -400));
      await tester.pump();
      await tester.pump();
      expect(int.parse(events.last.split(',').first), greaterThan(0));
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'modal escape dismisses without submitting, disabled button cannot act',
    (tester) async {
      final actions = <String>[];
      final model = RetainedModel((n, v) => actions.add(n));
      model.setTree(
        jsonEncode(
          node(
            '1',
            'Window',
            {'dialog': 'true'},
            [
              node('2', 'VerticalLayout', {}, [
                node('3', 'Edit', {'text': '0.125', 'height': '30'}),
                node('4', 'Button', {
                  'text': 'disabled',
                  'height': '30',
                  'enabled': 'false',
                }),
                node('5', 'Control', {}),
              ]),
            ],
          ),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pump();
      await tester.tap(find.text('disabled'));
      await tester.pump();
      expect(actions.where((e) => e == '4:click'), isEmpty);
      await tester.tap(find.byType(TextField));
      await tester.pump();
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pump();
      expect(actions.where((e) => e == '0:close').length, 1);
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'pane header drag preserves button hit target and reports bounds',
    (tester) async {
      final actions = <String>[];
      final model = RetainedModel((n, v) => actions.add('$n=$v'));
      model.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node(
              '2',
              'PaneCanvas',
              {'drag_height': '40'},
              [
                node(
                  '3',
                  'Pane',
                  {'name': 'test', 'bounds': '0,0,300,200'},
                  [
                    node(
                      '4',
                      'HorizontalLayout',
                      {'height': '40'},
                      [
                        node('5', 'Label', {'text': 'header'}),
                        node('6', 'Button', {'text': 'action', 'width': '80'}),
                      ],
                    ),
                    node('7', 'Control', {}),
                  ],
                ),
              ],
            ),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pump();
      await tester.tap(find.text('action'));
      await tester.pump();
      expect(actions, contains('6:click='));
      await tester.dragFrom(const Offset(40, 20), const Offset(100, 80));
      await tester.pump();
      expect(
        actions.any(
          (v) => v.startsWith('2:pane=test:') && !v.contains('test:0,0,'),
        ),
        isTrue,
      );
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets('semantic button theme and ancestor disabled input', (
    tester,
  ) async {
    final actions = <String>[];
    final model = RetainedModel((n, v) => actions.add(n));
    model.setTree(
      jsonEncode(
        node(
          '1',
          'Window',
          {'theme': 'xp-blue'},
          [
            node(
              '2',
              'VerticalLayout',
              {'enabled': 'false'},
              [
                node('3', 'Edit', {'text': '1', 'height': '30'}),
                node('4', 'Button', {
                  'text': 'themed',
                  'height': '30',
                  'role': 'button',
                }),
                node('5', 'Control', {}),
              ],
            ),
          ],
        ),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: model));
    await tester.pump();
    await tester.tap(find.byType(TextField), warnIfMissed: false);
    await tester.pump();
    expect(
      tester.widget<TextField>(find.byType(TextField)).focusNode!.hasFocus,
      isFalse,
    );
    await tester.tap(find.text('themed'), warnIfMissed: false);
    await tester.pump();
    expect(actions.where((e) => e == '4:click'), isEmpty);
    final decorations = tester
        .widgetList<DecoratedBox>(find.byType(DecoratedBox))
        .map((w) => w.decoration);
    expect(
      decorations.whereType<BoxDecoration>().any(
        (d) =>
            d.gradient is LinearGradient &&
            (d.gradient as LinearGradient).colors.last ==
                const Color(0xffe7e4d5),
      ),
      isTrue,
    );
    expect(tester.takeException(), isNull);
  });
  testWidgets(
    'fixed labels retain render object while text and semantics change',
    (tester) async {
      final semantics = tester.ensureSemantics();
      final model = RetainedModel((_, _) {});
      model.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'Label', {
              'text': '9007199254740993',
              'textcolor': '#123456',
            }),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pump();
      final label = find.byType(PlainLabel),
          render = tester.renderObject(label);
      for (var i = 0; i < 30; ++i) {
        model.patch(
          jsonEncode([
            {
              'id': '2',
              'attrs': {'text': '1.1000$i', 'textcolor': '#654321'},
            },
          ]),
        );
        await tester.pump();
      }
      expect(tester.renderObject(label), same(render));
      expect(find.bySemanticsLabel('1.100029'), findsOneWidget);
      expect((render as PlainLabelRender).node.text('text'), '1.100029');
      expect(tester.takeException(), isNull);
      semantics.dispose();
    },
  );
  test('themes resolve semantic tokens and explicit override', () {
    final p = UiPalette.from(
      RNode('1', 'Window', {
        'theme': 'xp-blue',
        'theme_tokens': '{"accent":"#123456"}',
      }, []),
    );
    expect(p.color(r'$surface'), const Color(0xffece9d8));
    expect(p.color(r'$accent'), const Color(0xff123456));
  });
}
