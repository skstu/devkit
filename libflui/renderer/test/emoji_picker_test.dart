import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';
import 'package:devkit_flui_renderer/emoji_picker.dart';

Map<String, Object> node(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};

Map<String, String> picker = {
  'target': 'draft',
  'glyph': 'emoji',
  'strokewidth': '1.4',
  'width': '28',
  'height': '28',
  'text': 'All emoji',
  'hint': 'Recently used',
  'tooltip': 'Choose an emoji',
};
Map<String, String> edit = {
  'name': 'draft',
  'text': 'before 中文 after',
  'maxchar': '40',
  'height': '70',
  'multiline': 'true',
};

RetainedModel model(List<(String, String)> events) {
  final m = RetainedModel((name, value) => events.add((name, value)));
  m.setTree(
    jsonEncode(
      node('1', 'Window', {}, [
        node('2', 'VerticalLayout', {}, [
          node('3', 'Control', {}),
          node('4', 'Edit', edit),
          node('5', 'EmojiPicker', picker),
        ]),
      ]),
    ),
  );
  return m;
}

void main() {
  testWidgets(
    'picker replaces selection, preserves caret, updates draft and recent',
    (tester) async {
      final events = <(String, String)>[];
      final m = model(events);
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pump();
      await tester.tap(find.byType(TextField));
      final field = tester.widget<TextField>(find.byType(TextField));
      field.controller!.selection = const TextSelection(
        baseOffset: 7,
        extentOffset: 9,
      );
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsOneWidget);
      expect(field.focusNode!.hasFocus, isTrue);
      await tester.tap(find.text('😀'));
      await tester.pumpAndSettle();
      expect(field.controller!.text, 'before 😀 after');
      expect(field.controller!.selection.extentOffset, 9);
      expect(events, contains(('4:valuechanged', 'before 😀 after')));
      expect(events, contains(('5:itemselect', '😀')));
      expect(m.nodes['4']!.text('text'), 'before 😀 after');
      expect(field.focusNode!.hasFocus, isTrue);
      expect(find.text('All emoji'), findsNothing);
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      expect(find.text('Recently used'), findsOneWidget);
      expect(find.text('😀'), findsNWidgets(2));
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsNothing);
      expect(field.controller!.text, 'before 😀 after');
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'keyboard selection commits composing text and respects grapheme limit',
    (tester) async {
      final events = <(String, String)>[];
      final m = model(events);
      m.patch(
        jsonEncode([
          {
            'id': '4',
            'attrs': {...edit, 'text': '中', 'maxchar': '2'},
          },
        ]),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.tap(find.byType(TextField));
      final field = tester.widget<TextField>(find.byType(TextField));
      field.controller!.value = const TextEditingValue(
        text: '中',
        selection: TextSelection.collapsed(offset: 1),
        composing: TextRange(start: 0, end: 1),
      );
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      await tester.sendKeyEvent(LogicalKeyboardKey.arrowRight);
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.pumpAndSettle();
      expect(field.controller!.text, '中😃');
      expect(field.controller!.selection.extentOffset, 3);
      expect(field.controller!.value.composing, TextRange.empty);
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      await tester.tap(find.text('😁'));
      await tester.pumpAndSettle();
      expect(field.controller!.text, '中😃');
      expect(find.text('All emoji'), findsOneWidget);
      // Outside click dismisses without selecting or emitting another insertion.
      await tester.tapAt(const Offset(700, 20));
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsNothing);
      expect(events.where((e) => e.$1 == '4:valuechanged').length, 1);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'readonly, disabled ancestors and draft resets cannot insert into another draft',
    (tester) async {
      final events = <(String, String)>[];
      final m = model(events);
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsOneWidget);
      m.patch(
        jsonEncode([
          {
            'id': '4',
            'attrs': {...edit, 'text': 'another draft', 'edit_revision': '1'},
          },
        ]),
      );
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsNothing);
      m.patch(
        jsonEncode([
          {
            'id': '4',
            'attrs': {...edit, 'readonly': 'true'},
          },
        ]),
      );
      await tester.pump();
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsNothing);
      m.patch(
        jsonEncode([
          {'id': '4', 'attrs': edit},
        ]),
      );
      await tester.pump();
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      m.patch(
        jsonEncode([
          {
            'id': '2',
            'attrs': {'enabled': 'false'},
          },
        ]),
      );
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsNothing);
      expect(events.where((e) => e.$1 == '4:valuechanged'), isEmpty);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('narrow popup stays inside viewport and scrolls to last emoji', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final m = model([]);
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.tap(find.byType(EmojiPickerSurface));
    await tester.pumpAndSettle();
    final rect = tester.getRect(find.byType(SingleChildScrollView));
    expect(rect.left, greaterThanOrEqualTo(0));
    expect(rect.right, lessThanOrEqualTo(390));
    expect(rect.top, greaterThanOrEqualTo(0));
    expect(rect.bottom, lessThan(720));
    await tester.drag(
      find.byType(SingleChildScrollView),
      const Offset(0, -600),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text('☕'));
    await tester.pumpAndSettle();
    expect(m.nodes['4']!.text('text'), endsWith('☕'));
    expect(tester.takeException(), isNull);
  });

  testWidgets('keepalive page switch dismisses overlay', (tester) async {
    final m = RetainedModel((_, _) {});
    m.setTree(
      jsonEncode(
        node('1', 'Window', {}, [
          node(
            '2',
            'TabLayout',
            {'selectedid': 'chat', 'keepalive': 'true'},
            [
              node(
                '3',
                'VerticalLayout',
                {'name': 'chat'},
                [
                  node('4', 'Control', {}),
                  node('5', 'Edit', edit),
                  node('6', 'EmojiPicker', picker),
                ],
              ),
              node('7', 'Control', {'name': 'settings'}),
            ],
          ),
        ]),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.tap(find.byType(EmojiPickerSurface));
    await tester.pumpAndSettle();
    expect(find.text('All emoji'), findsOneWidget);
    m.patch(
      jsonEncode([
        {
          'id': '2',
          'attrs': {'selectedid': 'settings', 'keepalive': 'true'},
        },
      ]),
    );
    await tester.pumpAndSettle();
    expect(find.text('All emoji'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('large page emits a preview event without changing the draft', (
    tester,
  ) async {
    final events = <(String, String)>[];
    final m = model(events);
    m.patch(
      jsonEncode([
        {
          'id': '5',
          'attrs': {...picker, 'largepage': 'true'},
        },
      ]),
    );
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.tap(find.byType(TextField));
    final field = tester.widget<TextField>(find.byType(TextField));
    final before = const TextEditingValue(
      text: 'before 中文 after',
      selection: TextSelection(baseOffset: 7, extentOffset: 9),
      composing: TextRange(start: 7, end: 9),
    );
    field.controller!.value = before;
    await tester.tap(find.byType(EmojiPickerSurface));
    await tester.pumpAndSettle();
    expect(tester.widget<Text>(find.text('😀')).style!.fontSize, 24);
    await tester.tap(find.text('Large'));
    await tester.pumpAndSettle();
    expect(tester.widget<Text>(find.text('😀')).style!.fontSize, 64);
    await tester.tap(find.text('😀'));
    await tester.pumpAndSettle();
    expect(events, contains(('5:largeitemselect', '😀')));
    expect(
      events.where((e) => e.$1 == '4:valuechanged' || e.$1 == '5:itemselect'),
      isEmpty,
    );
    expect(field.controller!.value, before);
    expect(m.nodes['4']!.text('text'), before.text);
    expect(m.emojiRecent, isEmpty);
    expect(m.emojiLargeRecent, ['😀']);
    expect(field.focusNode!.hasFocus, isTrue);
    await tester.tap(find.byType(EmojiPickerSurface));
    await tester.pumpAndSettle();
    expect(tester.widget<Text>(find.text('😀')).style!.fontSize, 24);
    expect(find.text('Recently used'), findsNothing);
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.pumpAndSettle();
    expect(find.text('Recently used'), findsOneWidget);
    expect(find.text('😀'), findsNWidgets(2));
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
  });

  testWidgets(
    'large grid fits narrow viewport and resets with the active draft',
    (tester) async {
      tester.view.physicalSize = const Size(390, 720);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final events = <(String, String)>[];
      final m = model(events);
      m.patch(
        jsonEncode([
          {
            'id': '5',
            'attrs': {
              ...picker,
              'largepage': 'true',
              'normaltext': 'Normal',
              'largetext': 'Large',
            },
          },
        ]),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pumpAndSettle();
      final rect = tester.getRect(find.byType(SingleChildScrollView));
      expect(rect.left, greaterThanOrEqualTo(0));
      expect(rect.right, lessThanOrEqualTo(390));
      expect(rect.top, greaterThanOrEqualTo(0));
      expect(rect.bottom, lessThan(720));
      await tester.drag(
        find.byType(SingleChildScrollView),
        const Offset(0, -4000),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.text('☕'));
      await tester.pumpAndSettle();
      expect(events, contains(('5:largeitemselect', '☕')));
      expect(m.nodes['4']!.text('text'), edit['text']);
      await tester.tap(find.byType(EmojiPickerSurface));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Large'));
      await tester.pumpAndSettle();
      m.patch(
        jsonEncode([
          {
            'id': '4',
            'attrs': {
              ...edit,
              'text': 'another conversation',
              'edit_revision': '1',
            },
          },
        ]),
      );
      await tester.pumpAndSettle();
      expect(find.text('All emoji'), findsNothing);
      expect(events.where((e) => e.$1 == '5:largeitemselect').length, 1);
      expect(tester.takeException(), isNull);
    },
  );
}
