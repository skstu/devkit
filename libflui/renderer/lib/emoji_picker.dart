import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

// Plain Unicode, rendered by the platform emoji font. No remote or proprietary
// artwork, assets, telemetry, persistent history or application send action.
const defaultEmoji = [
  '😀',
  '😃',
  '😄',
  '😁',
  '😆',
  '😅',
  '😂',
  '🤣',
  '🙂',
  '🙃',
  '😉',
  '😊',
  '🥰',
  '😍',
  '🤩',
  '😘',
  '😋',
  '😛',
  '😜',
  '🤪',
  '🤗',
  '🤭',
  '🤫',
  '🤔',
  '🫡',
  '😐',
  '😑',
  '😶',
  '😏',
  '🙄',
  '😬',
  '😌',
  '😔',
  '😴',
  '🤤',
  '😷',
  '🤒',
  '🤕',
  '🤢',
  '🤧',
  '🥵',
  '🥶',
  '😵',
  '🤯',
  '😎',
  '🥳',
  '😕',
  '🙁',
  '☹️',
  '😮',
  '😯',
  '😲',
  '😳',
  '🥺',
  '😧',
  '😨',
  '😰',
  '😥',
  '😢',
  '😭',
  '😱',
  '😖',
  '😣',
  '😞',
  '😓',
  '😩',
  '😤',
  '😡',
  '😠',
  '🤬',
  '👋',
  '🤚',
  '✋',
  '🖐️',
  '👌',
  '✌️',
  '🤞',
  '🤟',
  '🤘',
  '🤙',
  '👍',
  '👎',
  '✊',
  '👊',
  '👏',
  '🙌',
  '👐',
  '🤝',
  '🙏',
  '💪',
  '❤️',
  '🧡',
  '💛',
  '💚',
  '💙',
  '💜',
  '🖤',
  '🤍',
  '💔',
  '💕',
  '🎉',
  '🎊',
  '✨',
  '⭐',
  '🔥',
  '🌷',
  '🌹',
  '☀️',
  '🌙',
  '☕',
];

class EmojiPickerSurface extends StatefulWidget {
  const EmojiPickerSurface({
    super.key,
    required this.revision,
    required this.valid,
    required this.editorRevision,
    required this.recent,
    required this.items,
    required this.label,
    required this.recentLabel,
    required this.allLabel,
    required this.background,
    required this.foreground,
    required this.muted,
    required this.border,
    required this.accent,
    required this.radius,
    required this.onSelect,
    required this.restoreFocus,
    required this.child,
    this.largePage = false,
    this.normalLabel = 'Normal',
    this.largeLabel = 'Large',
    this.largeRecent = const [],
    this.onLargeSelect,
  });
  final ValueListenable<int> revision;
  final bool Function() valid;
  final String Function() editorRevision;
  final List<String> recent, items;
  final String label, recentLabel, allLabel;
  final Color background, foreground, muted, border, accent;
  final double radius;
  final bool Function(String) onSelect;
  final VoidCallback restoreFocus;
  final Widget child;
  final bool largePage;
  final String normalLabel, largeLabel;
  final List<String> largeRecent;
  final bool Function(String)? onLargeSelect;

  @override
  State<EmojiPickerSurface> createState() => _EmojiPickerSurfaceState();
}

class _EmojiPickerSurfaceState extends State<EmojiPickerSurface> {
  final portal = OverlayPortalController();
  final anchor = GlobalKey();
  final tapGroup = Object();
  final choiceKeys = <int, GlobalKey>{};
  bool open = false, large = false;
  int selected = 0, columns = 10;
  String editRevision = '';
  List<String> get catalog =>
      widget.items.isEmpty ? defaultEmoji : widget.items;
  List<String> get recent => (large ? widget.largeRecent : widget.recent)
      .where(catalog.contains)
      .toList();
  List<String> get choices => [...recent, ...catalog];

  @override
  void initState() {
    super.initState();
    widget.revision.addListener(documentChanged);
  }

  @override
  void didUpdateWidget(EmojiPickerSurface old) {
    super.didUpdateWidget(old);
    if (old.revision != widget.revision) {
      old.revision.removeListener(documentChanged);
      widget.revision.addListener(documentChanged);
    }
  }

  void documentChanged() {
    if (!open || !mounted) return;
    // A hidden page or application draft reset cannot keep an orphaned picker.
    if (!widget.valid() || widget.editorRevision() != editRevision) {
      close();
    } else {
      setState(() {});
    }
  }

  void toggle() {
    if (open) {
      close(restore: true);
      return;
    }
    if (!widget.valid()) return;
    editRevision = widget.editorRevision();
    selected = 0;
    large = false;
    choiceKeys.clear();
    open = true;
    FocusManager.instance.addEarlyKeyEventHandler(key);
    portal.show();
  }

  void close({bool restore = false}) {
    if (!open) return;
    open = false;
    FocusManager.instance.removeEarlyKeyEventHandler(key);
    portal.hide();
    if (restore) widget.restoreFocus();
  }

  void choose(String emoji) {
    if (widget.valid() &&
        (large
            ? widget.onLargeSelect?.call(emoji) == true
            : widget.onSelect(emoji)))
      close(restore: true);
  }

  void switchPage(bool value) {
    if (large == value) return;
    setState(() {
      large = value;
      selected = 0;
      choiceKeys.clear();
    });
  }

  KeyEventResult key(KeyEvent event) {
    if (!open || event is! KeyDownEvent) return KeyEventResult.ignored;
    if (event.logicalKey == LogicalKeyboardKey.escape) {
      close(restore: true);
      return KeyEventResult.handled;
    }
    if (widget.largePage && event.logicalKey == LogicalKeyboardKey.tab) {
      switchPage(!large);
      return KeyEventResult.handled;
    }
    if (event.logicalKey == LogicalKeyboardKey.enter) {
      if (choices.isNotEmpty)
        choose(choices[selected.clamp(0, choices.length - 1)]);
      return KeyEventResult.handled;
    }
    final delta = switch (event.logicalKey) {
      LogicalKeyboardKey.arrowLeft => -1,
      LogicalKeyboardKey.arrowRight => 1,
      LogicalKeyboardKey.arrowUp => -columns,
      LogicalKeyboardKey.arrowDown => columns,
      _ => 0,
    };
    if (delta == 0) return KeyEventResult.ignored;
    setState(() => selected = (selected + delta).clamp(0, choices.length - 1));
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final target = choiceKeys[selected]?.currentContext;
      if (open && target != null)
        Scrollable.ensureVisible(target, alignment: .5);
    });
    return KeyEventResult.handled;
  }

  @override
  void dispose() {
    widget.revision.removeListener(documentChanged);
    FocusManager.instance.removeEarlyKeyEventHandler(key);
    super.dispose();
  }

  Widget grid(List<String> values, int offset, double cell) => Wrap(
    children: [
      for (var i = 0; i < values.length; i++)
        SizedBox(
          key: choiceKeys.putIfAbsent(offset + i, () => GlobalKey()),
          width: cell,
          height: large ? 88 : 40,
          child: Semantics(
            button: true,
            label: values[i],
            child: MouseRegion(
              cursor: SystemMouseCursors.click,
              child: GestureDetector(
                behavior: HitTestBehavior.opaque,
                onTap: () => choose(values[i]),
                child: DecoratedBox(
                  decoration: BoxDecoration(
                    color: selected == offset + i
                        ? widget.accent.withValues(alpha: .10)
                        : null,
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: Center(
                    child: Padding(
                      padding: const EdgeInsets.all(4),
                      child: FittedBox(
                        fit: BoxFit.scaleDown,
                        child: Text(
                          values[i],
                          style: TextStyle(
                            fontSize: large ? 64 : 24,
                            color: widget.foreground,
                          ),
                          textScaler: TextScaler.noScaling,
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
    ],
  );

  Widget overlay(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      final box = anchor.currentContext?.findRenderObject() as RenderBox?;
      final overlay =
          Overlay.of(context).context.findRenderObject() as RenderBox?;
      if (box == null || overlay == null || !box.hasSize)
        return const SizedBox.shrink();
      final rect = box.localToGlobal(Offset.zero, ancestor: overlay) & box.size;
      final width = math.min(440.0, math.max(0.0, constraints.maxWidth - 16));
      final above = math.max(0.0, rect.top - 16);
      final below = math.max(0.0, constraints.maxHeight - rect.bottom - 16);
      final upwards = above >= 350 || above >= below;
      final available = upwards ? above : below;
      final height = math.min(350.0, math.max(0.0, available));
      final left = rect.left.clamp(
        8.0,
        math.max(8.0, constraints.maxWidth - width - 8),
      );
      final top = upwards ? rect.top - height - 8 : rect.bottom + 8;
      columns = math.max(1, ((width - 24) / (large ? 88 : 40)).floor());
      final cell = math.max(1.0, (width - 24) / columns);
      return Stack(
        children: [
          Positioned(
            left: left.toDouble(),
            top: top,
            width: width,
            height: height,
            child: TextFieldTapRegion(
              child: TapRegion(
                groupId: tapGroup,
                onTapOutside: (_) => close(),
                child: Material(
                  color: widget.background,
                  elevation: 8,
                  shadowColor: Colors.black.withValues(alpha: .2),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(widget.radius),
                    side: BorderSide(color: widget.border),
                  ),
                  clipBehavior: Clip.antiAlias,
                  child: Column(
                    children: [
                      Expanded(
                        child: SingleChildScrollView(
                          key: ValueKey(large),
                          padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              if (recent.isNotEmpty) ...[
                                heading(widget.recentLabel),
                                grid(recent, 0, cell),
                                const SizedBox(height: 6),
                              ],
                              heading(widget.allLabel),
                              grid(catalog, recent.length, cell),
                            ],
                          ),
                        ),
                      ),
                      if (widget.largePage)
                        Container(
                          height: math.min(42.0, height / 2),
                          decoration: BoxDecoration(
                            border: Border(
                              top: BorderSide(color: widget.border),
                            ),
                          ),
                          child: Row(
                            children: [
                              pageTab(widget.normalLabel, false),
                              pageTab(widget.largeLabel, true),
                            ],
                          ),
                        ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    },
  );

  Widget pageTab(String label, bool value) => Expanded(
    child: Semantics(
      selected: large == value,
      button: true,
      label: label,
      child: InkWell(
        onTap: () => switchPage(value),
        child: Center(
          child: Text(
            label,
            style: TextStyle(
              fontSize: 13,
              color: large == value ? widget.accent : widget.muted,
              fontWeight: large == value ? FontWeight.w600 : FontWeight.normal,
            ),
          ),
        ),
      ),
    ),
  );

  Widget heading(String text) => Padding(
    padding: const EdgeInsets.fromLTRB(4, 8, 4, 10),
    child: Text(text, style: TextStyle(fontSize: 12, color: widget.muted)),
  );

  @override
  Widget build(BuildContext context) => OverlayPortal(
    controller: portal,
    overlayChildBuilder: overlay,
    child: TextFieldTapRegion(
      child: TapRegion(
        groupId: tapGroup,
        child: FocusableActionDetector(
          shortcuts: const {
            SingleActivator(LogicalKeyboardKey.enter): ActivateIntent(),
            SingleActivator(LogicalKeyboardKey.space): ActivateIntent(),
          },
          actions: {
            ActivateIntent: CallbackAction<ActivateIntent>(
              onInvoke: (_) {
                toggle();
                return null;
              },
            ),
          },
          child: Semantics(
            button: true,
            label: widget.label,
            child: MouseRegion(
              cursor: SystemMouseCursors.click,
              child: GestureDetector(
                key: anchor,
                behavior: HitTestBehavior.opaque,
                onTap: toggle,
                child: widget.child,
              ),
            ),
          ),
        ),
      ),
    ),
  );
}
