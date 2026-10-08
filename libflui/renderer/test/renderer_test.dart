import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/renderer.dart';

final xml = File('test/quotes.xml').readAsStringSync();
Map<String, dynamic> snapshot() => {
  'quotes': [
    {'id': 'eurusd', 'symbol': 'EUR/USD', 'bid': '1.08234', 'ask': '1.08246'},
    {'id': 'gold', 'symbol': 'GOLD', 'bid': '2350.10', 'ask': '2350.40'},
  ],
  'selected': 'eurusd',
  'selection': '当前：EUR/USD',
  'quantity': '1',
  'result': '待操作',
};
void main() {
  test('reject malformed XML and invalid state atomically', () {
    final model = UiModel((_, _) {});
    addTearDown(model.dispose);
    model.load(xml);
    model.setStateJson(jsonEncode(snapshot()));
    final old = model.document.value;
    for (final bad in [
      '<Window><Unknown/></Window>',
      '<Window><Label mystery="1"/></Window>',
      '<!DOCTYPE x [<!ENTITY x SYSTEM "file:///etc/passwd">]><Window><Label/></Window>',
      '<Window><VerticalLayout><Edit name="same"/><Label name="same"/></VerticalLayout></Window>',
      '<Window><Label width="NaN"/></Window>',
      '<Window><Label width="{size}"/></Window>',
    ]) {
      expect(() => model.load(bad), throwsFormatException);
      expect(model.document.value, same(old));
    }
    final prior = model.state;
    expect(() => model.setStateJson('{"quantity":1}'), throwsFormatException);
    expect(() => model.setStateJson('[]'), throwsFormatException);
    final duplicates = snapshot();
    (duplicates['quotes'] as List).add((duplicates['quotes'] as List).first);
    expect(
      () => model.setStateJson(jsonEncode(duplicates)),
      throwsFormatException,
    );
    expect(model.state, same(prior));
  });
  testWidgets(
    'XML list, input, action and result; quotes preserve focus and cursor',
    (tester) async {
      final events = <List<String>>[];
      final model = UiModel((name, value) => events.add([name, value]));
      addTearDown(model.dispose);
      model.load(xml);
      final state = snapshot();
      model.setStateJson(jsonEncode(state));
      await tester.pumpWidget(FluiApp(model: model));
      expect(find.text('1.08234'), findsOneWidget);
      await tester.tap(find.text('GOLD'));
      await tester.pump();
      expect(events.removeLast(), ['quote.select', 'gold']);
      await tester.enterText(find.byType(TextField), '2');
      expect(events.removeLast(), ['quantity.changed', '2']);
      // A delayed old snapshot must not overwrite active typing either.
      model.setStateJson(jsonEncode(snapshot()));
      await tester.pump();
      expect(
        tester.widget<TextField>(find.byType(TextField)).controller!.text,
        '2',
      );
      // A normal controller echoes accepted input. Quote-only snapshots never reset it.
      state['quantity'] = '2';
      model.setStateJson(jsonEncode(state));
      await tester.pump();
      final editor = tester
          .widget<TextField>(find.byType(TextField))
          .controller!;
      editor.selection = const TextSelection.collapsed(offset: 0);
      for (var i = 0; i < 20; i++) {
        (state['quotes'] as List).first['bid'] = '1.082${50 + i}';
        model.setStateJson(jsonEncode(state));
        await tester.pump();
      }
      expect(editor.text, '2');
      expect(editor.selection.baseOffset, 0);
      expect(
        tester
            .widget<EditableText>(find.byType(EditableText))
            .focusNode
            .hasFocus,
        isTrue,
      );
      await tester.tap(find.text('模拟提交'));
      await tester.pump();
      expect(events.removeLast(), ['order.submit', '']);
      state['result'] = '模拟完成：GOLD × 2';
      model.setStateJson(jsonEncode(state));
      await tester.pump();
      expect(find.text('模拟完成：GOLD × 2'), findsOneWidget);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    },
  );
}
