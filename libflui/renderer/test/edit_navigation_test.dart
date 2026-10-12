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

void main() {
  for (final platform in [TargetPlatform.android, TargetPlatform.macOS]) {
    testWidgets('$platform multiline form traverses output, input and action', (
      tester,
    ) async {
      debugDefaultTargetPlatformOverride = platform;
      addTearDown(() => debugDefaultTargetPlatformOverride = null);
      late RetainedModel m;
      var revision = 0;
      final events = <String>[];
      m = RetainedModel((name, value) {
        events.add('$name=$value');
        if (name.endsWith(':navigate')) {
          m.patch(
            jsonEncode([
              {
                'id': name.split(':').first,
                'attrs': {
                  ...m.nodes[name.split(':').first]!.attrs,
                  'traverse': '$value${++revision}',
                },
              },
            ]),
          );
        }
      });
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Edit', {
                'height': '80',
                'multiline': 'true',
                'readonly': 'true',
                'event_navigate': 'true',
              }),
              node('4', 'Edit', {
                'height': '80',
                'multiline': 'true',
                'event_navigate': 'true',
              }),
              node('5', 'Button', {'height': '40', 'text': 'Use peer info'}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      final fields = tester
          .widgetList<TextField>(find.byType(TextField))
          .toList();
      fields[0].focusNode!.requestFocus();
      await tester.pump();
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pumpAndSettle();
      expect(events, contains('3:navigate=forward'));
      expect(fields[1].focusNode!.hasFocus, isTrue);
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pumpAndSettle();
      expect(events, contains('4:navigate=forward'));
      expect(fields[1].focusNode!.hasFocus, isFalse);
      await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
      await tester.pumpAndSettle();
      expect(fields[1].focusNode!.hasFocus, isTrue);
      expect(tester.takeException(), isNull);
      debugDefaultTargetPlatformOverride = null;
    });
  }
}
