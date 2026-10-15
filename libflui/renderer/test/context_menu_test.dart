import 'dart:convert';
import 'dart:ui' show PointerDeviceKind;
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> node(String id, String tag, Map<String, String> attrs,
        [List<Map<String, Object>> children = const []]) =>
    {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};

void main() {
  for (final mouse in [true, false]) {
    testWidgets('context menu ${mouse ? 'secondary click' : 'long press'} preserves row selection and target', (tester) async {
      final events = <String>[];
      final model = RetainedModel((name, value) => events.add('$name=$value'));
      final rowAttrs = {'height': '80', 'event_context': 'true', 'contextkey': 'remove'};
      model.setTree(jsonEncode(node('1', 'Window', {
        'locale': 'zh-Hans',
        'translations': jsonEncode({'zh-Hans': {'remove': '移出会话列表'}}),
      }, [node('2', 'List', {'event_itemclick': 'true'}, [
        node('3', 'ListRow', rowAttrs, [node('4', 'Label', {'text': 'Peer'})]),
      ])])));
      await tester.pumpWidget(RetainedApp(model: model));
      await tester.pumpAndSettle();
      Future<void> open() async {
        final position = tester.getCenter(find.byKey(const ValueKey('3')));
        if (mouse) {
          final gesture = await tester.startGesture(position, kind: PointerDeviceKind.mouse, buttons: kSecondaryMouseButton);
          await gesture.up();
        } else {
          await tester.longPressAt(position);
        }
        await tester.pumpAndSettle();
        expect(find.text('移出会话列表'), findsOneWidget);
      }
      await open();
      expect(events.where((e) => e.contains('itemclick') || e.contains(':context')), isEmpty);
      await tester.tapAt(const Offset(790, 590));
      await tester.pumpAndSettle();
      expect(events.where((e) => e.contains(':context')), isEmpty);
      await open();
      await tester.tap(find.text('移出会话列表'));
      await tester.pumpAndSettle();
      expect(events.where((e) => e.contains(':context')), ['3:context=']);
      expect(events.where((e) => e.contains('itemclick')), isEmpty);
      events.clear();
      // A pending menu cannot act on a row disabled or replaced while open.
      await open();
      model.patch(jsonEncode([{'id': '3', 'attrs': {...rowAttrs, 'enabled': 'false'}}]));
      await tester.pump();
      await tester.tap(find.text('移出会话列表'));
      await tester.pumpAndSettle();
      expect(events.where((e) => e.contains(':context')), isEmpty);
      model.patch(jsonEncode([{'id': '3', 'attrs': rowAttrs}]));
      await tester.pump();
      await open();
      model.setTree(jsonEncode(node('1', 'Window', {}, [node('9', 'Label', {'text': 'Replacement'})])));
      await tester.pump();
      await tester.tap(find.text('移出会话列表'));
      await tester.pumpAndSettle();
      expect(events.where((e) => e.contains(':context')), isEmpty);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    });
  }
}
