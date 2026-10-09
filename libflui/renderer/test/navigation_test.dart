import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> node(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};
void main() {
  test('SVG is local vector data; invalid patches cannot replace a valid mark', () {
    final m = RetainedModel((_, _) {});
    const svg =
        '<svg viewBox="0 0 100 100"><defs><mask id="clear"><rect width="100" height="100" fill="white"/><circle cx="50" cy="50" r="10" fill="black"/></mask></defs><path mask="url(#clear)" d="M0 50h100" stroke="black"/></svg>';
    m.setTree(
      jsonEncode(
        node('1', 'Window', {}, [
          node('7', 'Svg', {'svg': svg}),
        ]),
      ),
    );
    for (final bad in [
      '<svg><image href="https://example.com/a.png"/></svg>',
      '<svg><use href="file:///secret"/></svg>',
      '<svg><script/></svg>',
      '<svg><path fill="url(https://example.com)"/></svg>',
      '<not-svg/>',
    ]) {
      expect(
        () => m.patch(
          jsonEncode([
            {
              'id': '7',
              'attrs': {'svg': bad},
            },
          ]),
        ),
        throwsA(isA<FormatException>()),
      );
      expect(m.nodes['7']!.text('svg'), svg);
    }
  });
  testWidgets(
    'retained tab keeps draft, selection and scroll but releases hidden focus',
    (tester) async {
      final m = RetainedModel((_, _) {});
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node(
              '2',
              'TabLayout',
              {'selectedid': '3', 'keepalive': 'true'},
              [
                node(
                  '3',
                  'VerticalLayout',
                  {'name': '3'},
                  [
                    node(
                      '4',
                      'List',
                      {'height': '200', 'showheader': 'false'},
                      [
                        for (var i = 0; i < 30; i++)
                          node(
                            '${100 + i}',
                            'ListRow',
                            {'height': '30'},
                            [
                              node('${200 + i}', 'Label', {'text': 'row $i'}),
                            ],
                          ),
                      ],
                    ),
                    node('5', 'Edit', {'text': 'draft 中文', 'height': '40'}),
                  ],
                ),
                node('6', 'Label', {'name': '6', 'text': 'Discover'}),
              ],
            ),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pump();
      final edit = tester.widget<TextField>(find.byType(TextField));
      await tester.tap(find.byType(TextField));
      await tester.pump();
      edit.controller!.selection = const TextSelection(
        baseOffset: 0,
        extentOffset: 5,
      );
      final scroll = tester.state<ScrollableState>(
        find.byType(Scrollable).first,
      );
      scroll.position.jumpTo(210);
      await tester.pump();
      expect(edit.focusNode!.hasFocus, isTrue);
      m.patch(
        jsonEncode([
          {
            'id': '2',
            'attrs': {'selectedid': '6', 'keepalive': 'true'},
          },
        ]),
      );
      await tester.pump();
      await tester.pump();
      expect(edit.focusNode!.hasFocus, isFalse);
      expect(find.byType(TextField), findsNothing);
      m.patch(
        jsonEncode([
          {
            'id': '2',
            'attrs': {'selectedid': '3', 'keepalive': 'true'},
          },
        ]),
      );
      await tester.pump();
      await tester.pump();
      expect(
        tester.widget<TextField>(find.byType(TextField)).controller,
        same(edit.controller),
      );
      expect(edit.controller!.text, 'draft 中文');
      expect(
        edit.controller!.selection,
        const TextSelection(baseOffset: 0, extentOffset: 5),
      );
      expect(scroll.position.pixels, 210);
      expect(edit.focusNode!.hasFocus, isFalse);
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets('SVG masks and tint render without async parser errors', (
    tester,
  ) async {
    final m = RetainedModel((_, _) {});
    m.setTree(
      jsonEncode(
        node('1', 'Window', {}, [
          node('7', 'Svg', {
            'textcolor': '#dddddd',
            'svg': '<svg viewBox="0 0 100 100"><defs><mask id="m"><rect width="100" height="100" fill="white"/><circle cx="50" cy="50" r="10" fill="black"/></mask></defs><rect mask="url(#m)" width="100" height="100" fill="black"/></svg>',
          }),
        ]),
      ),
    );
    await tester.pumpWidget(RetainedApp(model: m));
    await tester.runAsync(() async {
      await Future<void>.delayed(const Duration(milliseconds: 100));
    });
    await tester.pump();
    expect(
      tester.getSize(find.byKey(const ValueKey('7'))).width,
      greaterThan(0),
    );
    expect(tester.takeException(), isNull);
  });
}
