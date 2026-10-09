import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/gestures.dart';
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
  testWidgets(
    'shrinking message viewport keeps latest or viewed history above composer',
    (tester) async {
      final m = RetainedModel((_, _) {});
      Map<String, String> attrs(int height) => {
        'height': '$height',
        'showheader': 'false',
        'scrollanchor': 'bottom',
      };
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'List', attrs(400), [
                for (var i = 0; i < 40; i++)
                  node(
                    '${100 + i}',
                    'ListRow',
                    {'height': '${30 + i % 3 * 7}'},
                    [
                      node('${200 + i}', 'Label', {'text': 'row $i'}),
                    ],
                  ),
              ]),
              node('4', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pump();
      final position = tester
          .state<ScrollableState>(find.byType(Scrollable))
          .position;
      position.jumpTo(100000);
      await tester.pumpAndSettle();
      expect(position.pixels, closeTo(position.maxScrollExtent, .01));
      m.patch(
        jsonEncode([
          {'id': '3', 'attrs': attrs(220)},
        ]),
      );
      await tester.pumpAndSettle();
      expect(position.pixels, closeTo(position.maxScrollExtent, .01));
      final listRect = tester.getRect(find.byKey(const ValueKey('3')));
      final lastRect = tester.getRect(find.byKey(const ValueKey('239')));
      expect(lastRect.bottom, lessThanOrEqualTo(listRect.bottom + .01));
      expect(lastRect.bottom, greaterThan(listRect.top));
      position.jumpTo(150);
      await tester.pumpAndSettle();
      final visibleEnd = position.pixels + position.viewportDimension;
      m.patch(
        jsonEncode([
          {'id': '3', 'attrs': attrs(120)},
        ]),
      );
      await tester.pumpAndSettle();
      expect(
        position.pixels + position.viewportDimension,
        closeTo(visibleEnd, .01),
      );
      expect(position.pixels, lessThan(position.maxScrollExtent));
      expect(m.nodes['3']!.scrollOffset, closeTo(position.pixels, .01));
      m.patch(
        jsonEncode([
          {'id': '3', 'attrs': attrs(300)},
        ]),
      );
      await tester.pumpAndSettle();
      expect(
        position.pixels + position.viewportDimension,
        closeTo(visibleEnd, .01),
      );
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'resize displacement survives moving handle and preserves editor composing state',
    (tester) async {
      final events = <(String, String)>[];
      late RetainedModel m;
      m = RetainedModel((name, value) {
        if (!name.startsWith('4:resize')) return;
        events.add((name, value));
        if (name == '4:resize') {
          m.patch(
            jsonEncode([
              {
                'id': '3',
                'attrs': {'height': '${200 + int.parse(value)}'},
              },
            ]),
          );
        }
      });
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Control', {'height': '200'}),
              node('4', 'ResizeHandle', {'height': '10', 'tooltip': 'Resize'}),
              node('5', 'Edit', {
                'text': 'draft 中文',
                'multiline': 'true',
                'valign': 'top',
              }),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pump();
      final field = tester.widget<TextField>(find.byType(TextField));
      await tester.tap(find.byType(TextField));
      await tester.pump();
      const value = TextEditingValue(
        text: 'draft 中文',
        selection: TextSelection(baseOffset: 6, extentOffset: 8),
        composing: TextRange(start: 6, end: 8),
      );
      field.controller!.value = value;
      final gesture = await tester.startGesture(
        tester.getCenter(find.byType(ResizeHandleSurface)),
        kind: PointerDeviceKind.mouse,
      );
      await gesture.moveBy(const Offset(0, 24));
      await tester.pump();
      await gesture.moveBy(const Offset(0, 30));
      await tester.pump();
      final first = int.parse(events.last.$2);
      await gesture.moveBy(const Offset(0, 36));
      await tester.pump();
      expect(int.parse(events.last.$2) - first, 36);
      await gesture.up();
      await tester.pump();
      expect(events.first, ('4:resizestart', '0'));
      expect(events.last.$1, '4:resizeend');
      expect(field.controller!.value, value);
      expect(field.focusNode!.hasFocus, isTrue);
      expect(
        tester.widget<TextField>(find.byType(TextField)).controller,
        same(field.controller),
      );
      expect(field.textAlignVertical, TextAlignVertical.top);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('keyboard resize and disabled ancestors', (tester) async {
    final events = <String>[];
    final m = RetainedModel((name, value) {
      if (name.contains('resize')) events.add('$name=$value');
    });
    m.setTree(
      jsonEncode(
        node('1', 'Window', {}, [
          node('2', 'VerticalLayout', {}, [
            node('3', 'ResizeHandle', {
              'height': '12',
              'direction': 'horizontal',
              'tooltip': 'Resize',
            }),
            node('4', 'Control', {}),
          ]),
        ]),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.pump();
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.pump();
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowRight);
    await tester.pump();
    expect(events, ['3:resizestart=0', '3:resize=8', '3:resizeend=8']);
    m.patch(
      jsonEncode([
        {
          'id': '2',
          'attrs': {'enabled': 'false'},
        },
      ]),
    );
    await tester.pump();
    events.clear();
    await tester.drag(find.byType(ResizeHandleSurface), const Offset(80, 0));
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowRight);
    await tester.pump();
    expect(events, isEmpty);
    expect(tester.takeException(), isNull);
  });
}
