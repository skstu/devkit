import 'dart:convert';

import 'package:flutter/gestures.dart';
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

const rowAttrs = {
  'height': '64',
  'inset': '24,10,18,10',
  'event_click': 'true',
  'bkcolor': r'$panel',
  'hotbkcolor': r'$hover',
  'focusbkcolor': r'$focus',
};
const rootAttrs = {
  'theme': 'light',
  'theme_tokens': '{"panel":"#f1f1f1","hover":"#e2e2e2","focus":"#dddddd","selected":"#16ad76"}',
  'theme_dark_tokens': '{"panel":"#242424","hover":"#303030","focus":"#363636","selected":"#087d45"}',
};
Finder row([String id = '3']) => find.byKey(ValueKey(id));
Color? background(
  WidgetTester tester, [
  String id = '3',
  bool offstage = false,
]) {
  final boxes = find.descendant(
    of: find.byKey(ValueKey(id), skipOffstage: !offstage),
    matching: find.byType(DecoratedBox, skipOffstage: !offstage),
    skipOffstage: !offstage,
  );
  return (tester.widgetList<DecoratedBox>(boxes).first.decoration
          as BoxDecoration)
      .color;
}

void patch(RetainedModel model, String id, Map<String, String> attrs) =>
    model.patch(
      jsonEncode([
        {'id': id, 'attrs': attrs},
      ]),
    );

void main() {
  testWidgets('hover and selected patches cover the padded clickable row', (
    tester,
  ) async {
    final actions = <String>[];
    final model = RetainedModel((name, value) => actions.add('$name=$value'));
    model.setTree(
      jsonEncode(
        node('1', 'Window', rootAttrs, [
          node('2', 'VerticalLayout', {}, [
            node('3', 'HorizontalLayout', rowAttrs, [
              node('4', 'Label', {'text': 'Settings'}),
            ]),
            node('5', 'Control', {}),
          ]),
        ]),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: model));
    expect(background(tester), const Color(0xfff1f1f1));
    final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
    await mouse.addPointer(location: const Offset(700, 500));
    await mouse.moveTo(tester.getTopLeft(row()) + const Offset(2, 2));
    await tester.pump();
    expect(background(tester), const Color(0xffe2e2e2));
    expect(actions.where((a) => a == '3:click='), isEmpty);
    await mouse.down(tester.getTopLeft(row()) + const Offset(2, 2));
    await mouse.up();
    await tester.pump();
    expect(
      actions.where((a) => a == '3:click=').length,
      1,
      reason: 'The inset area must activate the row too',
    );
    patch(model, '3', {
      ...rowAttrs,
      'bkcolor': r'$selected',
      'hotbkcolor': r'$selected',
      'focusbkcolor': r'$selected',
    });
    await tester.pump();
    expect(background(tester), const Color(0xff16ad76));
    await mouse.moveTo(const Offset(700, 500));
    await tester.pump();
    expect(background(tester), const Color(0xff16ad76));
    await mouse.removePointer();
    expect(tester.takeException(), isNull);
  });

  testWidgets(
    'keyboard focus takes precedence and survives theme/selection patches',
    (tester) async {
      final actions = <String>[];
      final model = RetainedModel((name, value) => actions.add('$name=$value'));
      model.setTree(
        jsonEncode(
          node('1', 'Window', rootAttrs, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'HorizontalLayout', rowAttrs, [
                node('4', 'Label', {'text': 'General'}),
              ]),
              node('5', 'HorizontalLayout', rowAttrs, [
                node('6', 'Label', {'text': 'Shortcuts'}),
              ]),
              node('7', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pump();
      expect(background(tester), const Color(0xffdddddd));
      final focused = FocusManager.instance.primaryFocus;
      final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
      await mouse.addPointer(location: tester.getCenter(row()));
      await tester.pump();
      expect(background(tester), const Color(0xffdddddd));
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.sendKeyEvent(LogicalKeyboardKey.space);
      expect(actions.where((a) => a == '3:click=').length, 2);
      patch(model, '1', {...rootAttrs, 'theme': 'dark'});
      await tester.pump();
      expect(background(tester), const Color(0xff363636));
      expect(FocusManager.instance.primaryFocus, same(focused));
      patch(model, '3', {
        ...rowAttrs,
        'bkcolor': r'$selected',
        'hotbkcolor': r'$selected',
        'focusbkcolor': r'$selected',
      });
      await tester.pump();
      expect(background(tester), const Color(0xff087d45));
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pump();
      expect(background(tester), const Color(0xff087d45));
      expect(background(tester, '5'), const Color(0xff363636));
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      expect(actions.where((a) => a == '5:click=').length, 1);
      await mouse.removePointer();
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'disabled ancestors and inactive pages clear highlight and activation',
    (tester) async {
      final actions = <String>[];
      final model = RetainedModel((name, value) => actions.add('$name=$value'));
      model.setTree(
        jsonEncode(
          node('1', 'Window', rootAttrs, [
            node(
              '2',
              'TabLayout',
              {'selectedid': 'first', 'keepalive': 'true'},
              [
                node(
                  '8',
                  'VerticalLayout',
                  {'name': 'first'},
                  [
                    node('3', 'HorizontalLayout', rowAttrs, [
                      node('4', 'Label', {'text': 'General'}),
                    ]),
                    node('9', 'Control', {}),
                  ],
                ),
                node(
                  '10',
                  'VerticalLayout',
                  {'name': 'second'},
                  [
                    node('5', 'HorizontalLayout', rowAttrs, [
                      node('6', 'Label', {'text': 'About'}),
                    ]),
                    node('7', 'Control', {}),
                  ],
                ),
              ],
            ),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pump();
      expect(background(tester), const Color(0xffdddddd));
      patch(model, '8', {'name': 'first', 'enabled': 'false'});
      await tester.pump();
      expect(background(tester), const Color(0xfff1f1f1));
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.tapAt(tester.getCenter(row()));
      await tester.pump();
      expect(actions.where((a) => a.endsWith(':click=')), isEmpty);
      patch(model, '8', {'name': 'first'});
      patch(model, '2', {'selectedid': 'second', 'keepalive': 'true'});
      await tester.pump();
      expect(background(tester, '3', true), const Color(0xfff1f1f1));
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pump();
      expect(background(tester, '5'), const Color(0xffdddddd));
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      expect(actions.where((a) => a == '3:click='), isEmpty);
      expect(actions.where((a) => a == '5:click=').length, 1);
      patch(model, '5', {...rowAttrs, 'visible': 'false'});
      await tester.pump();
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      expect(actions.where((a) => a == '5:click=').length, 1);
      expect(tester.takeException(), isNull);
    },
  );
}
