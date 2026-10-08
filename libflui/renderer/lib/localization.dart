import 'dart:convert';
import 'dart:ui';

import 'package:intl/intl.dart';

// Application catalogs are data. User content never passes through this lookup.
class UiStrings {
  UiStrings(this.locale, this.fallback, this.catalogs);
  final String locale, fallback;
  final Map<String, dynamic> catalogs;
  static String canonical(String value) {
    final v = value.replaceAll('_', '-');
    final parts = v.toLowerCase().split('-');
    if (parts.first == 'zh')
      return parts.contains('hant') || parts.any({'tw', 'hk', 'mo'}.contains)
          ? 'zh-Hant'
          : 'zh-Hans';
    return parts.first;
  }

  static void validate(Object? value) {
    if (value is! Map || value.length > 128)
      throw const FormatException('Invalid language catalogs');
    for (final entry in value.entries) {
      if (entry.key is! String || entry.key.length > 64 || entry.value is! Map)
        throw const FormatException('Invalid language catalog');
      for (final message in (entry.value as Map).entries) {
        if (message.key is! String || message.key.length > 256)
          throw const FormatException('Invalid translation key');
        final text = message.value;
        if (text is String) continue;
        if (text is! Map ||
            text['other'] is! String ||
            text.entries.any(
              (e) =>
                  !{
                    'zero',
                    'one',
                    'two',
                    'few',
                    'many',
                    'other',
                  }.contains(e.key) ||
                  e.value is! String,
            ))
          throw const FormatException('Invalid plural message');
      }
    }
  }

  factory UiStrings.from(Map<String, String> attrs, List<Locale> system) {
    final data = jsonDecode(attrs['translations'] ?? '{}');
    validate(data);
    final catalogs = Map<String, dynamic>.from(data as Map);
    final fallback = canonical(attrs['fallback_locale'] ?? 'en');
    var requested = attrs['locale'] ?? 'system';
    if (requested == 'system')
      requested = system
          .map((v) => canonical(v.toLanguageTag()))
          .firstWhere(catalogs.containsKey, orElse: () => fallback);
    final locale = canonical(requested);
    return UiStrings(
      catalogs.containsKey(locale) ? locale : fallback,
      fallback,
      catalogs,
    );
  }
  Locale get flutterLocale {
    final p = locale.split('-');
    return Locale.fromSubtags(
      languageCode: p.first,
      scriptCode: p.length > 1 ? p[1] : null,
    );
  }

  String lookup(String key, Map<String, String> args) {
    final local = (catalogs[locale] as Map?)?[key];
    final translated = local ?? (catalogs[fallback] as Map?)?[key];
    final messageLocale = local == null ? fallback : locale;
    String text;
    if (translated is Map) {
      final count = num.tryParse(args['count'] ?? '');
      if (count == null || !count.isFinite) return '[$key:count]';
      text = Intl.pluralLogic(
        count,
        locale: messageLocale,
        zero: translated['zero'],
        one: translated['one'],
        two: translated['two'],
        few: translated['few'],
        many: translated['many'],
        other: translated['other'],
      ) as String;
    } else {
      text = translated is String ? translated : '[$key]';
    }
    return text.replaceAllMapped(
      RegExp(r'\{([A-Za-z0-9_.]+)\}'),
      (m) => args[m[1]] ?? m[0]!,
    );
  }

  String field(Map<String, String> attrs, String field) {
    final key = attrs['${field}key'];
    if (key == null || key.isEmpty) return attrs[field] ?? '';
    final args = Map<String, String>.from(
      jsonDecode(attrs['${field}args'] ?? '{}') as Map,
    );
    return lookup(key, args);
  }
}
