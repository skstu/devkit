import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> part(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {
  'id': {
    'root': '1',
    'send': '2',
    'page': '3',
    'heading': '4',
    'form': '5',
    'group': '6',
    'help': '7',
    'name': '8',
    'footer': '9',
    'save': '10',
  }[id]!,
  'tag': tag,
  'attrs': {...attrs, 'name': id},
  'children': children,
};

void main() {
  testWidgets('system back is opt-in and returns through the public event', (
    t,
  ) async {
    final actions = <String>[];
    final m = RetainedModel((n, v) {
      if (n == '1:back') actions.add(v);
    });
    m.setTree(
      jsonEncode(
        part(
          'root',
          'Window',
          {'event_back': 'true'},
          [
            part('heading', 'Label', {'text': 'Detail', 'height': '48', 'wordwrap': 'true'}),
          ],
        ),
      ),
    );
    await t.pumpWidget(RetainedApp(model: m));
    await t.pump();
    expect(find.text('Detail', findRichText: true), findsOneWidget);
    expect(await t.binding.handlePopRoute(), isTrue);
    await t.pump();
    expect(actions.length, 1);
    expect(find.text('Detail', findRichText: true), findsOneWidget);
    m.patch(
      jsonEncode([
        {
          'id': '1',
          'attrs': {...m.nodes['1']!.attrs, 'event_back': 'false'},
        },
      ]),
    );
    await t.pump();
    await t.binding.handlePopRoute();
    await t.pump();
    expect(actions.length, 1);
    expect(t.takeException(), isNull);
  });

  testWidgets('disabled surface follows its token and cannot submit', (
    t,
  ) async {
    final actions = <String>[];
    final m = RetainedModel((n, v) {
      if (n == '2:click') actions.add(v);
    });
    m.setTree(
      jsonEncode(
        part(
          'root',
          'Window',
          {
            'theme_tokens': jsonEncode({'disabledBg': '#343A3E'}),
          },
          [
            part('send', 'Button', {
              'text': 'Send',
              'enabled': 'false',
              'event_click': 'true',
              'bkcolor': '#087F5B',
              'disabledbkcolor': r'$disabledBg',
            }),
          ],
        ),
      ),
    );
    await t.pumpWidget(RetainedApp(model: m));
    await t.pump();
    Color? background() {
      final boxes = t.widgetList<DecoratedBox>(
        find.descendant(
          of: find.byKey(const ValueKey('2')),
          matching: find.byType(DecoratedBox),
        ),
      );
      return boxes
          .map((b) => b.decoration)
          .whereType<BoxDecoration>()
          .map((b) => b.color)
          .whereType<Color>()
          .firstOrNull;
    }

    expect(background(), const Color(0xff343a3e));
    await t.tap(find.text('Send'), warnIfMissed: false);
    await t.pump();
    expect(actions, isEmpty);
    m.patch(
      jsonEncode([
        {
          'id': '2',
          'attrs': {...m.nodes['2']!.attrs, 'enabled': 'true'},
        },
      ]),
    );
    await t.pump();
    expect(background(), const Color(0xff087f5b));
    await t.tap(find.text('Send'));
    await t.pump();
    expect(actions.length, 1);
    expect(t.takeException(), isNull);
  });

  for (final scale in [1.0, 1.5]) {
    testWidgets(
      'small scroll form fits text and keeps save above IME at $scale',
      (t) async {
        t.view.physicalSize = const Size(320, 568);
        t.view.devicePixelRatio = 1;
        t.platformDispatcher.textScaleFactorTestValue = scale;
        addTearDown(t.view.reset);
        addTearDown(t.platformDispatcher.clearTextScaleFactorTestValue);
        final m = RetainedModel((_, __) {});
        m.setTree(
          jsonEncode(
            part(
              'root',
              'Window',
              {'safe_area': 'true'},
              [
                part('page', 'VerticalLayout', {}, [
                  part('heading', 'Label', {'text': '我的设备', 'height': '48'}),
                  part('form', 'ScrollLayout', {}, [
                    part(
                      'group',
                      'VerticalLayout',
                      {'autoheight': 'true', 'childpadding': '12'},
                      [
                        part('help', 'Label', {
                          'text': List.filled(12, '只读编号可复制，设备名字可以修改。').join(),
                          'autoheight': 'true',
                          'wordwrap': 'true',
                          'fontsize': '16',
                        }),
                        part('name', 'Edit', {'text': '保留的名字', 'height': '48'}),
                      ],
                    ),
                  ]),
                  part(
                    'footer',
                    'HorizontalLayout',
                    {'height': '64'},
                    [
                      part('save', 'Button', {'text': '保存', 'width': '100'}),
                    ],
                  ),
                ]),
              ],
            ),
          ),
        );
        await t.pumpWidget(RetainedApp(model: m));
        await t.pump();
        expect(
          t.getSize(find.byKey(const ValueKey('7'))).height,
          greaterThan(80),
        );
        expect(t.takeException(), isNull);
        final input = t.widget<TextField>(find.byType(TextField));
        final controller = input.controller;
        t.view.viewInsets = const FakeViewPadding(bottom: 240);
        await t.pump();
        await t.pump();
        expect(
          t.getBottomRight(find.byKey(const ValueKey('10'))).dy,
          lessThanOrEqualTo(328),
        );
        expect(
          t.widget<TextField>(find.byType(TextField)).controller,
          same(controller),
        );
        expect(controller!.text, '保留的名字');
        expect(t.takeException(), isNull);
      },
    );
  }
}
