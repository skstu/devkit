import 'dart:convert';

import 'package:flutter/foundation.dart';
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
  testWidgets('single-line editors center text inside their background', (
    tester,
  ) async {
    addTearDown(tester.view.reset);
    addTearDown(() => debugDefaultTargetPlatformOverride = null);
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(360, 240);
    for (final platform in [
      TargetPlatform.android,
      TargetPlatform.iOS,
      TargetPlatform.macOS,
    ]) {
      debugDefaultTargetPlatformOverride = platform;
      for (final height in [36, 40, 56]) {
        for (final readOnly in [false, true]) {
          final model = RetainedModel((_, __) {});
          model.setTree(
            jsonEncode(
              node('1', 'Window', {}, [
                node('2', 'VerticalLayout', {}, [
                  node('3', 'Edit', {
                    'height': '$height',
                    'inset': '8,6,8,6',
                    'fontsize': '14',
                    'text': 'Device 123',
                    'readonly': '$readOnly',
                    'bkcolor': '#FFFFFFFF',
                  }),
                  node('4', 'Control', {}),
                ]),
              ]),
            ),
          );
          await tester.pumpWidget(RetainedApp(model: model));
          await tester.pumpAndSettle();
          final editable = tester
              .state<EditableTextState>(find.byType(EditableText))
              .renderEditable;
          final caret = editable.getLocalRectForCaret(
            const TextPosition(offset: 0),
          );
          final center = editable.localToGlobal(caret.center).dy;
          final background = tester.getRect(find.byKey(const ValueKey('3')));
          expect(
            center,
            closeTo(background.center.dy, 1.5),
            reason: '$platform height=$height readonly=$readOnly',
          );
          expect(tester.takeException(), isNull);
          await tester.pumpWidget(const SizedBox.shrink());
        }
      }
    }
    debugDefaultTargetPlatformOverride = null;
  });

  for (final platform in [
    TargetPlatform.android,
    TargetPlatform.iOS,
    TargetPlatform.macOS,
  ]) {
    testWidgets(
      '$platform editor measures real wraps, caps and scrolls without losing IME',
      (tester) async {
        debugDefaultTargetPlatformOverride = platform;
        addTearDown(() => debugDefaultTargetPlatformOverride = null);
        tester.view.devicePixelRatio = 1;
        tester.view.physicalSize = const Size(180, 600);
        addTearDown(tester.view.resetPhysicalSize);
        addTearDown(tester.view.resetDevicePixelRatio);
        final measurements = <int>[];
        late RetainedModel model;
        void patch(Map<String, String> attrs) => model.patch(
          jsonEncode([
            {
              'id': '3',
              'attrs': {...model.nodes['3']!.attrs, ...attrs},
            },
          ]),
        );
        model = RetainedModel((name, value) {
          if (name == '3:valuechanged') patch({'text': value});
          if (name == '3:textmeasure') {
            final size = value.split(',').map(int.parse).toList();
            measurements.add(size[1]);
            // A consumer owns its cap; the generic Edit only reports its content.
            patch({'height': '${(size[1] + 14).clamp(48, 158)}'});
          }
        });
        model.setTree(
          jsonEncode(
            node('1', 'Window', {}, [
              node('2', 'VerticalLayout', {}, [
                node('3', 'Edit', {
                  'width': '180',
                  'height': '48',
                  'inset': '8,7,8,7',
                  'multiline': 'true',
                  'vscrollbar': 'true',
                  'valign': 'top',
                  'fontsize': '14',
                  'emojiscale': '1.4',
                  'maxchar': '2000',
                  'event_valuechanged': 'true',
                  'event_textmeasure': 'true',
                  'event_enter': 'true',
                  'submitkey': 'ctrl-enter',
                }),
                node('4', 'Control', {}),
              ]),
            ]),
          ),
        );
        await tester.pumpWidget(RetainedApp(model: model));
        await tester.pumpAndSettle();
        final emptyHeight = measurements.last;
        final fieldFinder = find.byType(TextField);
        final controller = tester.widget<TextField>(fieldFinder).controller!;
        expect(model.nodes['3']!.text('height'), '48');
        await tester.enterText(fieldFinder, 'a');
        await tester.pumpAndSettle();
        expect(measurements.last, emptyHeight);
        expect(model.nodes['3']!.text('height'), '48');
        await tester.enterText(fieldFinder, 'a\nb\nc');
        await tester.pumpAndSettle();
        expect(measurements.last, greaterThan(emptyHeight * 2));
        expect(int.parse(model.nodes['3']!.text('height')), greaterThan(48));
        final threeLines = measurements.last;
        await tester.enterText(fieldFinder, 'a\nb\nc\n');
        await tester.pumpAndSettle();
        expect(measurements.last, greaterThan(threeLines));
        // Soft wrapping changes when the same text is given less width.
        await tester.enterText(
          fieldFinder,
          'abcdef ghijkl mnopqr stuvwx yz0123 456789',
        );
        await tester.pumpAndSettle();
        final wideHeight = measurements.last;
        tester.view.physicalSize = const Size(90, 600);
        await tester.pumpAndSettle();
        expect(measurements.last, greaterThan(wideHeight));
        await tester.enterText(fieldFinder, List.filled(40, 'line').join('\n'));
        await tester.pumpAndSettle();
        final field = tester.widget<TextField>(fieldFinder);
        expect(model.nodes['3']!.text('height'), '158');
        expect(
          field.scrollController!.position.maxScrollExtent,
          greaterThan(0),
        );
        expect(find.byType(Scrollbar), findsWidgets);
        expect(identical(field.controller, controller), isTrue);
        // Resizing during composition must not replace the draft or selection.
        const composing = TextEditingValue(
          text: '中文输入 ni',
          selection: TextSelection.collapsed(offset: 7),
          composing: TextRange(start: 5, end: 7),
        );
        tester.testTextInput.updateEditingValue(composing);
        await tester.pumpAndSettle();
        tester.view.physicalSize = const Size(180, 600);
        await tester.pumpAndSettle();
        expect(controller.value, composing);
        expect(field.focusNode!.hasFocus, isTrue);
        // A consumer clear after send collapses even while focused.
        patch({'text': '', 'edit_revision': '1'});
        await tester.pumpAndSettle();
        expect(controller.text, isEmpty);
        expect(model.nodes['3']!.text('height'), '48');
        expect(field.scrollController!.position.maxScrollExtent, 0);
        expect(tester.takeException(), isNull);
        debugDefaultTargetPlatformOverride = null;
        await tester.pumpWidget(const SizedBox());
        await tester.pump();
      },
    );
  }
}
