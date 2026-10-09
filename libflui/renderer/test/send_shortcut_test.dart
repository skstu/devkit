import 'dart:convert';

import 'package:flutter/foundation.dart';
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

Future<void> chord(
  WidgetTester tester, {
  bool control = false,
  bool shift = false,
  bool meta = false,
  LogicalKeyboardKey key = LogicalKeyboardKey.enter,
}) async {
  if (control) await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
  if (shift) await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
  if (meta) await tester.sendKeyDownEvent(LogicalKeyboardKey.metaLeft);
  await tester.sendKeyEvent(
    key,
    platform: key == LogicalKeyboardKey.numpadEnter ? 'macos' : null,
  );
  if (meta) await tester.sendKeyUpEvent(LogicalKeyboardKey.metaLeft);
  if (shift) await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
  if (control) await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
  await tester.pump();
}

void main() {
  for (final platform in [
    TargetPlatform.macOS,
    TargetPlatform.windows,
    TargetPlatform.linux,
  ]) {
    for (final shortcut in ['enter', 'ctrl-enter']) {
      testWidgets(
        '$platform $shortcut submits once and inserts alternate newlines',
        (tester) async {
          debugDefaultTargetPlatformOverride = platform;
          addTearDown(() => debugDefaultTargetPlatformOverride = null);
          final actions = <String>[];
          final m = RetainedModel((name, value) => actions.add('$name=$value'));
          final attrs = {
            'height': '120',
            'multiline': 'true',
            'submitkey': shortcut,
            'event_enter': 'true',
            'event_valuechanged': 'true',
            'maxchar': '2000',
          };
          m.setTree(
            jsonEncode(
              node('1', 'Window', {}, [
                node('2', 'VerticalLayout', {}, [
                  node('3', 'Edit', attrs),
                  node('4', 'Control', {}),
                ]),
              ]),
            ),
          );
          await tester.pumpWidget(RetainedApp(model: m));
          await tester.enterText(find.byType(TextField), '中文😀 draft');
          final field = tester.widget<TextField>(find.byType(TextField));
          final controller = field.controller!;
          final original = controller.value;
          final control = shortcut == 'ctrl-enter';
          await chord(tester, control: control);
          expect(actions.where((a) => a == '3:enter=').length, 1);
          expect(
            controller.value,
            original,
            reason: 'Submitting does not edit the captured draft',
          );
          await chord(tester, control: !control);
          expect(controller.text, '中文😀 draft\n');
          await chord(tester, shift: true);
          expect(controller.text, '中文😀 draft\n\n');
          expect(actions.where((a) => a == '3:enter=').length, 1);
          expect(actions, contains('3:valuechanged=中文😀 draft\n\n'));
          await chord(
            tester,
            control: control,
            key: LogicalKeyboardKey.numpadEnter,
          );
          expect(actions.where((a) => a == '3:enter=').length, 2);
          if (control)
            await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
          await tester.sendKeyDownEvent(LogicalKeyboardKey.enter);
          await tester.sendKeyRepeatEvent(LogicalKeyboardKey.enter);
          await tester.sendKeyRepeatEvent(LogicalKeyboardKey.enter);
          await tester.sendKeyUpEvent(LogicalKeyboardKey.enter);
          if (control)
            await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
          expect(actions.where((a) => a == '3:enter=').length, 3);
          await chord(tester, meta: true);
          expect(actions.where((a) => a == '3:enter=').length, 3);
          await tester.testTextInput.receiveAction(TextInputAction.newline);
          await tester.testTextInput.receiveAction(TextInputAction.done);
          expect(actions.where((a) => a == '3:enter=').length, 3);
          expect(tester.takeException(), isNull);
          debugDefaultTargetPlatformOverride = null;
        },
      );
    }
  }

  testWidgets('IME candidates and read-only/disabled inputs cannot submit', (
    tester,
  ) async {
    final actions = <String>[];
    final m = RetainedModel((name, value) => actions.add('$name=$value'));
    final attrs = {
      'height': '100',
      'multiline': 'true',
      'submitkey': 'enter',
      'event_enter': 'true',
    };
    m.setTree(
      jsonEncode(
        node('1', 'Window', {}, [
          node('2', 'VerticalLayout', {}, [
            node('3', 'Edit', attrs),
            node('4', 'Control', {}),
          ]),
        ]),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.enterText(find.byType(TextField), 'ni');
    final controller = tester
        .widget<TextField>(find.byType(TextField))
        .controller!;
    controller.value = const TextEditingValue(
      text: 'ni',
      selection: TextSelection.collapsed(offset: 2),
      composing: TextRange(start: 0, end: 2),
    );
    await chord(tester);
    await chord(tester, control: true);
    expect(actions.where((a) => a == '3:enter='), isEmpty);
    controller.value = const TextEditingValue(
      text: '你',
      selection: TextSelection.collapsed(offset: 1),
    );
    await chord(tester);
    expect(actions.where((a) => a == '3:enter=').length, 1);
    m.patch(
      jsonEncode([
        {
          'id': '3',
          'attrs': {...attrs, 'readonly': 'true'},
        },
      ]),
    );
    await tester.pump();
    await chord(tester);
    expect(actions.where((a) => a == '3:enter=').length, 1);
    m.patch(
      jsonEncode([
        {
          'id': '2',
          'attrs': {'enabled': 'false'},
        },
      ]),
    );
    await tester.pump();
    await chord(tester);
    expect(actions.where((a) => a == '3:enter=').length, 1);
    expect(tester.takeException(), isNull);
  });

  testWidgets(
    'switching mode keeps selection, newline respects grapheme limit',
    (tester) async {
      final actions = <String>[];
      final m = RetainedModel((name, value) => actions.add('$name=$value'));
      final attrs = {
        'height': '100',
        'multiline': 'true',
        'submitkey': 'ctrl-enter',
        'event_enter': 'true',
        'event_valuechanged': 'true',
        'maxchar': '3',
      };
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Edit', attrs),
              node('4', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.enterText(find.byType(TextField), 'A👍🏽B');
      final controller = tester
          .widget<TextField>(find.byType(TextField))
          .controller!;
      await chord(tester);
      expect(
        controller.text,
        'A👍🏽B',
        reason: 'Newline cannot exceed maxchar',
      );
      controller.selection = const TextSelection(
        baseOffset: 1,
        extentOffset: 5,
      );
      await chord(tester);
      expect(controller.text, 'A\nB');
      expect(controller.selection.baseOffset, 2);
      final value = controller.value;
      m.patch(
        jsonEncode([
          {
            'id': '3',
            'attrs': {...attrs, 'submitkey': 'enter', 'text': controller.text},
          },
        ]),
      );
      await tester.pump();
      expect(
        tester.widget<TextField>(find.byType(TextField)).controller,
        same(controller),
      );
      expect(controller.value, value);
      await chord(tester);
      expect(actions, contains('3:enter='));
      expect(controller.value, value);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('ordinary multiline editors preserve default keyboard behavior', (
    tester,
  ) async {
    final actions = <String>[];
    final m = RetainedModel((name, value) => actions.add('$name=$value'));
    m.setTree(
      jsonEncode(
        node('1', 'Window', {}, [
          node('2', 'VerticalLayout', {}, [
            node('3', 'Edit', {
              'height': '100',
              'multiline': 'true',
              'event_enter': 'true',
            }),
            node('4', 'Control', {}),
          ]),
        ]),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.enterText(find.byType(TextField), 'old consumer');
    await chord(tester);
    expect(actions.where((a) => a == '3:enter='), isEmpty);
    await tester.testTextInput.receiveAction(TextInputAction.done);
    expect(actions, contains('3:enter='));
    expect(tester.takeException(), isNull);
  });
}
