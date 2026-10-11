import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> node(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};
const svg =
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 12h16"/></svg>';

void main() {
  testWidgets(
    'SVG action keeps the full target, keyboard action and focus outline',
    (tester) async {
      final events = <String>[];
      final m = RetainedModel((n, v) => events.add('$n=$v'));
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Icon', {
                'svg': svg,
                'iconsize': '20',
                'height': '48',
                'width': '48',
                'tooltip': 'Open chat',
                'event_click': 'true',
                'textcolor': '#123456',
                'disabledtextcolor': '#787878',
                'focusbordercolor': '#076f50',
              }),
              node('4', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pumpAndSettle();
      expect(find.byType(SvgPicture), findsOneWidget);
      final icon = find.byKey(const ValueKey('3'));
      final bounds = tester.getRect(icon);
      await tester.tapAt(bounds.topLeft + const Offset(3, 3));
      expect(events, contains('3:click='));
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pump();
      expect(
        tester.getRect(icon),
        bounds,
        reason: 'Focus must not move the target',
      );
      final focusedBoxes = tester.widgetList<DecoratedBox>(
        find.descendant(of: icon, matching: find.byType(DecoratedBox)),
      );
      expect(
        focusedBoxes.any(
          (b) =>
              b.position == DecorationPosition.foreground &&
              (b.decoration as BoxDecoration).border?.top.width == 2,
        ),
        isTrue,
      );
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      expect(events.where((e) => e == '3:click=').length, 2);
      m.patch(
        jsonEncode([
          {
            'id': '3',
            'attrs': {...m.nodes['3']!.attrs, 'enabled': 'false'},
          },
        ]),
      );
      await tester.pump();
      expect(
        tester.widget<SvgPicture>(find.byType(SvgPicture)).colorFilter,
        const ColorFilter.mode(Color(0xff787878), BlendMode.srcIn),
      );
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'copy is explicit, reports failure, and disabled actions do not write',
    (tester) async {
      final writes = <String>[];
      var fail = false;
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          if (call.method == 'Clipboard.setData') {
            if (fail) throw PlatformException(code: 'denied');
            writes.add((call.arguments as Map)['text'] as String);
          }
          return null;
        },
      );
      addTearDown(
        () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
          SystemChannels.platform,
          null,
        ),
      );
      final events = <String>[];
      final m = RetainedModel((n, v) => events.add('$n=$v'));
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Icon', {
                'svg': svg,
                'height': '48',
                'copytext': 'identity-123',
                'tooltip': 'Copy ID',
                'event_copyresult': 'true',
              }),
              node('4', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pumpAndSettle();
      expect(writes, isEmpty);
      await tester.tap(find.byKey(const ValueKey('3')));
      await tester.pump();
      expect(writes, ['identity-123']);
      expect(events, contains('3:copyresult=ok'));
      fail = true;
      await tester.tap(find.byKey(const ValueKey('3')));
      await tester.pump();
      expect(events, contains('3:copyresult=failed'));
      m.patch(
        jsonEncode([
          {
            'id': '3',
            'attrs': {...m.nodes['3']!.attrs, 'enabled': 'false'},
          },
        ]),
      );
      await tester.pump();
      final count = events.length;
      await tester.tap(find.byKey(const ValueKey('3')));
      await tester.pump();
      expect(events.length, count);
      expect(writes, ['identity-123']);
    },
  );
}
