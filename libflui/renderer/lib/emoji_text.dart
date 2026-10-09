import 'package:flutter/material.dart';

// Classify complete grapheme clusters using the engine's Unicode properties.
// Text-presentation symbols, ASCII digits and punctuation keep their text size.
final _emojiPresentation = RegExp(r'\p{Emoji_Presentation}', unicode: true);
final _emoji = RegExp(r'\p{Emoji}', unicode: true);

bool isEmojiCluster(String cluster) =>
    !cluster.contains('\uFE0E') &&
    (_emojiPresentation.hasMatch(cluster) ||
        cluster.contains('\u20E3') ||
        (cluster.contains('\uFE0F') && _emoji.hasMatch(cluster)));

TextSpan emojiTextSpan(String text, TextStyle style, double scale) {
  if (scale <= 1) return TextSpan(text: text, style: style);
  final runs = <TextSpan>[];
  var plain = '';
  void flush() {
    if (plain.isNotEmpty) {
      runs.add(TextSpan(text: plain));
      plain = '';
    }
  }

  for (final cluster in text.characters) {
    if (isEmojiCluster(cluster)) {
      flush();
      runs.add(
        TextSpan(
          text: cluster,
          style: TextStyle(fontSize: (style.fontSize ?? 14) * scale),
        ),
      );
    } else {
      plain += cluster;
    }
  }
  flush();
  return TextSpan(style: style, children: runs);
}

class EmojiEditingController extends TextEditingController {
  EmojiEditingController({super.text, this.emojiScale = 1});
  double emojiScale;

  @override
  TextSpan buildTextSpan({
    required BuildContext context,
    TextStyle? style,
    required bool withComposing,
  }) {
    final original = super.buildTextSpan(
      context: context,
      style: style,
      withComposing: withComposing,
    );
    if (emojiScale <= 1) return original;
    TextSpan expand(TextSpan span, TextStyle inherited) {
      final effective = inherited.merge(span.style);
      final converted = emojiTextSpan(span.text ?? '', effective, emojiScale);
      return TextSpan(
        style: span.style,
        children: [
          ...?converted.children,
          for (final child in span.children ?? <InlineSpan>[])
            if (child is TextSpan) expand(child, effective) else child,
        ],
      );
    }

    return expand(original, style ?? const TextStyle(fontSize: 14));
  }
}
