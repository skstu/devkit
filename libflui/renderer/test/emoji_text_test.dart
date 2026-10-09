import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/emoji_text.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String, Object> node(
  String id,
  String tag,
  Map<String, String> attrs, [
  List<Map<String, Object>> children = const [],
]) => {'id': id, 'tag': tag, 'attrs': attrs, 'children': children};

void main() {
  test(
    'enlarge emoji graphemes without styling ordinary digits or text symbols',
    () {
      for (final cluster in [
        '😀',
        '❤️',
        '1️⃣',
        '🇭🇰',
        '👨‍👩‍👧‍👦',
        '👍🏽',
      ]) {
        expect(isEmojiCluster(cluster), isTrue, reason: cluster);
      }
      for (final cluster in ['中', 'a', '1', '#', '©', '™', '❤︎']) {
        expect(isEmojiCluster(cluster), isFalse, reason: cluster);
      }
      const text = '中文12 © 😀👨‍👩‍👧‍👦👍🏽 后文';
      final span = emojiTextSpan(text, const TextStyle(fontSize: 14), 1.4);
      expect(span.toPlainText(), text);
      final runs = span.children!.cast<TextSpan>();
      expect(runs.where((s) => s.style?.fontSize != null).length, 3);
      for (final run in runs.where((s) => s.style?.fontSize != null)) {
        expect(run.style!.fontSize, closeTo(19.6, .001));
        expect(run.text!.characters.length, 1);
      }
      expect(span.style!.fontSize, 14);
    },
  );

  testWidgets(
    'changing emoji scale retains editor, selection and composition',
    (tester) async {
      final m = RetainedModel((_, _) {});
      final attrs = {
        'text': '输入 😀 test',
        'height': '100',
        'multiline': 'true',
        'fontsize': '14',
        'emojiscale': '1.4',
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
      await tester.tap(find.byType(TextField));
      final field = tester.widget<TextField>(find.byType(TextField));
      const value = TextEditingValue(
        text: '输入 😀 test',
        selection: TextSelection(baseOffset: 0, extentOffset: 2),
        composing: TextRange(start: 0, end: 2),
      );
      field.controller!.value = value;
      final span = field.controller!.buildTextSpan(
        context: tester.element(find.byType(TextField)),
        style: field.style,
        withComposing: true,
      );
      expect(span.toPlainText(), value.text);
      final composingRuns = <String>[];
      void collectComposing(TextSpan current, TextStyle inherited) {
        final effective = inherited.merge(current.style);
        if (effective.decoration == TextDecoration.underline &&
            current.text != null) {
          composingRuns.add(current.text!);
        }
        for (final child in current.children ?? <InlineSpan>[]) {
          if (child is TextSpan) collectComposing(child, effective);
        }
      }

      collectComposing(span, field.style!);
      expect(composingRuns.join(), '输入');
      m.patch(
        jsonEncode([
          {
            'id': '3',
            'attrs': {...attrs, 'emojiscale': '1.5'},
          },
        ]),
      );
      await tester.pump();
      final updated = tester.widget<TextField>(find.byType(TextField));
      expect(updated.controller, same(field.controller));
      expect(field.controller!.value, value);
      expect(field.focusNode!.hasFocus, isTrue);
      expect(updated.style!.fontSize, 14);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'wrapped emoji metrics match paragraph and refresh after text change',
    (tester) async {
      final metrics = <String>[];
      final m = RetainedModel((name, value) {
        if (name == '3:textmeasure') metrics.add(value);
      });
      final attrs = {
        'text': '正文 😀❤️👨‍👩‍👧‍👦👍🏽 多行 😀😀😀😀😀😀',
        'fontsize': '14',
        'emojiscale': '1.4',
        'height': '200',
        'width': '100',
        'wordwrap': 'true',
        'event_textmeasure': 'true',
      };
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'HorizontalLayout', {}, [
              node('3', 'Label', attrs),
              node('4', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pumpAndSettle();
      expect(metrics, isNotEmpty);
      final text = tester.widget<Text>(find.byType(Text));
      final painter = TextPainter(
        text: text.textSpan,
        textDirection: TextDirection.ltr,
      )..layout(maxWidth: 100);
      final reported = metrics.last.split(',').map(int.parse).toList();
      expect(reported.first, 100);
      expect(reported.last, painter.height.ceil());
      expect(reported.last, greaterThan(30));
      painter.dispose();
      final count = metrics.length;
      m.patch(
        jsonEncode([
          {
            'id': '3',
            'attrs': {
              ...attrs,
              'text': '正文 😃❤️👨‍👩‍👧‍👦👍🏽 多行 😀😀😀😀😀😀',
            },
          },
        ]),
      );
      await tester.pumpAndSettle();
      expect(metrics.length, greaterThan(count));
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'intrinsic widths fit short text and emoji independently of wrapping',
    (tester) async {
      final widths = <int>[];
      final heights = <int>[];
      final m = RetainedModel((name, value) {
        if (name == '3:textwidth') widths.add(int.parse(value));
        if (name == '3:textmeasure') {
          final fields = value.split(',');
          expect(fields.length, 2);
          heights.add(int.parse(fields.last));
        }
      });
      final attrs = <String, String>{
        'text': '1',
        'fontsize': '14',
        'emojiscale': '1.4',
        'width': '250',
        'height': '300',
        'wordwrap': 'true',
        'event_textwidth': 'true',
        'event_textmeasure': 'true',
      };
      m.setTree(
        jsonEncode(
          node('1', 'Window', {}, [
            node('2', 'HorizontalLayout', {}, [
              node('3', 'Label', attrs),
              node('4', 'Control', {}),
            ]),
          ]),
        ),
      );
      await tester.pumpWidget(RetainedApp(model: m));
      await tester.pumpAndSettle();
      expect(widths.last, inInclusiveRange(1, 30));
      Future<void> patch(Map<String, String> changes) async {
        attrs.addAll(changes);
        m.patch(
          jsonEncode([
            {
              'id': '3',
              'attrs': {...attrs},
            },
          ]),
        );
        await tester.pumpAndSettle();
      }

      await patch({'text': '😀'});
      final singleEmoji = widths.last;
      expect(singleEmoji, inInclusiveRange(1, 60));
      await patch({'text': '😀😀'});
      expect(widths.last, greaterThan(singleEmoji));
      await patch({'text': '短消息 😀 后文'});
      final naturalWidth = widths.last, unwrappedHeight = heights.last;
      await patch({'width': '40'});
      expect(widths.last, naturalWidth);
      expect(heights.last, greaterThan(unwrappedHeight));
      await patch({'fontsize': '20'});
      expect(widths.last, greaterThan(naturalWidth));
      expect(tester.takeException(), isNull);
    },
  );
}
