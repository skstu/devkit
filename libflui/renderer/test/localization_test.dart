import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/localization.dart';
import 'package:devkit_flui_renderer/retained.dart';

void main() {
  final catalogs = {
    'en': {
      'send': 'Send',
      'fallback': 'Only English',
      'file': 'File: {name}',
      'count': {'one': '{count} file', 'other': '{count} files'},
    },
    'zh-Hans': {
      'send': '发送',
      'count': {'other': '{count} 个文件'},
    },
    'zh-Hant': {'send': '傳送'},
  };
  Map<String, String> attrs(String locale) => {
    'locale': locale,
    'translations': jsonEncode(catalogs),
    'fallback_locale': 'en',
  };
  test('system Chinese scripts, explicit locale, fallback and parameters', () {
    expect(
      UiStrings.from(attrs('system'), [
        const Locale('zh', 'TW'),
      ]).lookup('send', {}),
      '傳送',
    );
    expect(
      UiStrings.from(attrs('system'), [
        const Locale('zh', 'CN'),
      ]).lookup('send', {}),
      '发送',
    );
    final strings = UiStrings.from(attrs('zh-Hant'), [const Locale('en')]);
    expect(strings.lookup('fallback', {}), 'Only English');
    expect(strings.lookup('count', {'count': '1'}), '1 file');
    expect(strings.lookup('missing', {}), '[missing]');
    expect(strings.lookup('file', {'name': '<中文>&.pdf'}), 'File: <中文>&.pdf');
    expect(
      UiStrings.from(attrs('en'), []).lookup('count', {'count': '1'}),
      '1 file',
    );
    expect(
      UiStrings.from(attrs('en'), []).lookup('count', {'count': '2'}),
      '2 files',
    );
    expect(
      UiStrings.from(attrs('zh-Hans'), []).lookup('count', {'count': '2'}),
      '2 个文件',
    );
  });
  testWidgets(
    'locale switch preserves focused composition and rejects bad catalogs atomically',
    (t) async {
      final model = RetainedModel((_, __) {});
      Map<String, Object> node(
        String id,
        String tag,
        Map<String, String> a, [
        List<Object> c = const [],
      ]) => {'id': id, 'tag': tag, 'attrs': a, 'children': c};
      model.setTree(
        jsonEncode(
          node('1', 'Window', attrs('en'), [
            node('2', 'VerticalLayout', {}, [
              node('3', 'Button', {'textkey': 'send', 'height': '40'}),
              node('4', 'Edit', {'hintkey': 'send', 'height': '50'}),
              node('5', 'Control', {}),
            ]),
          ]),
        ),
      );
      await t.pumpWidget(RetainedApp(model: model));
      await t.pumpAndSettle();
      await t.tap(find.byType(EditableText));
      await t.pump();
      final before = t.widget<EditableText>(find.byType(EditableText));
      const draft = TextEditingValue(
        text: '未完成中文',
        selection: TextSelection.collapsed(offset: 5),
        composing: TextRange(start: 3, end: 5),
      );
      before.controller.value = draft;
      for (final locale in ['zh-Hans', 'zh-Hant', 'en']) {
        model.patch(
          jsonEncode([
            {'id': '1', 'attrs': attrs(locale)},
          ]),
        );
        await t.pumpAndSettle();
        final editor = t.widget<EditableText>(find.byType(EditableText));
        expect(editor.controller, same(before.controller));
        expect(editor.controller.value, draft);
        expect(editor.focusNode.hasFocus, isTrue);
        expect(find.text(catalogs[locale]!['send'] as String), findsWidgets);
        expect(t.takeException(), isNull);
      }
      expect(
        () => model.patch(
          jsonEncode([
            {
              'id': '1',
              'attrs': {'translations': '{"en":{"bad":7}}'},
            },
          ]),
        ),
        throwsFormatException,
      );
      expect(model.root.value!.text('locale'), 'en');
    },
  );
  testWidgets(
    'dark popup stays readable and icon actions expose localized names',
    (t) async {
      final model = RetainedModel((_, __) {});
      model.setTree(
        jsonEncode({
          'id': '1',
          'tag': 'Window',
          'attrs': {...attrs('zh-Hans'), 'theme': 'dark'},
          'children': [
            {
              'id': '2',
              'tag': 'VerticalLayout',
              'attrs': {},
              'children': [
                {
                  'id': '3',
                  'tag': 'Icon',
                  'attrs': {
                    'glyph': 'chat',
                    'tooltipkey': 'send',
                    'event_click': 'true',
                    'height': '40',
                  },
                  'children': [],
                },
                {
                  'id': '4',
                  'tag': 'Combo',
                  'attrs': {
                    'items_json': '["简体中文","English"]',
                    'selected': '0',
                    'height': '40',
                  },
                  'children': [],
                },
                {'id': '5', 'tag': 'Control', 'attrs': {}, 'children': []},
              ],
            },
          ],
        }),
      );
      final semantics = t.ensureSemantics();
      await t.pumpWidget(RetainedApp(model: model));
      await t.pumpAndSettle();
      expect(find.bySemanticsLabel('发送'), findsWidgets);
      await t.tap(find.byType(DropdownButton<int>));
      await t.pumpAndSettle();
      final menuText = t.widgetList<Text>(find.text('English'));
      expect(menuText, isNotEmpty);
      for (final text in menuText) {
        expect(text.style!.color, const Color(0xffeeeeee));
      }
      expect(t.takeException(), isNull);
      semantics.dispose();
    },
  );
}
