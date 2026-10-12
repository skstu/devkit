import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> n(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};
void main() {
  for (final platform in [TargetPlatform.android, TargetPlatform.macOS]) {
    testWidgets(
      '$platform activates a list row by keyboard and respects disabled state',
      (t) async {
        debugDefaultTargetPlatformOverride = platform;
        addTearDown(() => debugDefaultTargetPlatformOverride = null);
        final events = <String>[];
        final m = RetainedModel((name, value) {
          events.add('$name=$value');
        });
        m.setTree(
          jsonEncode(
            n('1', 'Window', {}, [
              n('2', 'VerticalLayout', {}, [
                n('3', 'Button', {'text': 'Before', 'height': '32'}),
                n(
                  '4',
                  'List',
                  {
                    'height': '160',
                    'showheader': 'false',
                    'event_itemclick': 'true',
                    'selected': '1',
                  },
                  [
                    n(
                      '5',
                      'ListRow',
                      {'height': '50'},
                      [
                        n('6', 'Label', {'text': 'First device'}),
                      ],
                    ),
                    n(
                      '7',
                      'ListRow',
                      {'height': '50'},
                      [
                        n('8', 'Label', {'text': 'Second device'}),
                      ],
                    ),
                  ],
                ),
                n('9', 'Button', {'text': 'After', 'height': '32'}),
              ]),
            ]),
          ),
        );
        await t.pumpWidget(RetainedApp(model: m));
        await t.pumpAndSettle();
        Focus.of(t.element(find.text('Before'))).requestFocus();
        await t.pump();
        await t.sendKeyEvent(LogicalKeyboardKey.tab);
        await t.pumpAndSettle();
        await t.sendKeyEvent(LogicalKeyboardKey.enter);
        await t.pumpAndSettle();
        expect(events, contains('4:itemclick=0,0'));
        await t.sendKeyEvent(LogicalKeyboardKey.tab);
        await t.pumpAndSettle();
        await t.sendKeyEvent(LogicalKeyboardKey.space);
        await t.pumpAndSettle();
        expect(events, contains('4:itemclick=1,0'));
        m.patch(
          jsonEncode([
            {
              'id': '4',
              'attrs': {...m.nodes['4']!.attrs, 'enabled': 'false'},
            },
          ]),
        );
        await t.pumpAndSettle();
        final prior = events.where((v) => v.contains(':itemclick=')).length;
        await t.sendKeyEvent(LogicalKeyboardKey.enter);
        await t.pumpAndSettle();
        expect(events.where((v) => v.contains(':itemclick=')).length, prior);
        expect(t.takeException(), isNull);
        debugDefaultTargetPlatformOverride = null;
      },
    );
  }
}
