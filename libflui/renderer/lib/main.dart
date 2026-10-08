import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/scheduler.dart';

import 'renderer.dart';
import 'retained.dart';

const channel = MethodChannel('devkit.libflui/v1');
void main() {
  WidgetsFlutterBinding.ensureInitialized();
  final model = UiModel((name, value) {
    unawaited(
      channel.invokeMethod<void>('action', {'name': name, 'value': value}),
    );
  });
  final retained = RetainedModel((name, value) {
    unawaited(
      channel.invokeMethod<void>('action', {'name': name, 'value': value}),
    );
  });
  final timings = <String>[];
  var lastTiming = DateTime.now();
  SchedulerBinding.instance.addTimingsCallback((frames) {
    if (retained.root.value?.flag('profile') != true) {
      timings.clear();
      return;
    }
    for (final f in frames) {
      if (timings.length < 512)
        timings.add(
          '${f.buildDuration.inMicroseconds},${f.rasterDuration.inMicroseconds},${f.totalSpan.inMicroseconds}',
        );
    }
    final now = DateTime.now();
    if (now.difference(lastTiming).inMilliseconds >= 1000) {
      retained.action('0:frames', timings.join(';'));
      timings.clear();
      lastTiming = now;
    }
  });
  final treeMode = ValueNotifier(false);
  channel.setMethodCallHandler((call) async {
    try {
      if (call.arguments is! String)
        throw const FormatException('Expected UTF-8 text');
      switch (call.method) {
        case 'tree':
          retained.setTree(call.arguments as String);
          treeMode.value = true;
        case 'patch':
          retained.patch(call.arguments as String);
        case 'load':
          model.load(call.arguments as String);
        case 'state':
          model.setStateJson(call.arguments as String);
        default:
          throw MissingPluginException('Unknown operation');
      }
    } on FormatException catch (error) {
      throw PlatformException(code: 'invalid_document', message: error.message);
    }
  });
  FlutterError.onError = (details) {
    FlutterError.presentError(details);
    unawaited(channel.invokeMethod<void>('error', details.exceptionAsString()));
  };
  runApp(
    ValueListenableBuilder<bool>(
      valueListenable: treeMode,
      builder: (_, tree, _) =>
          tree ? RetainedApp(model: retained) : FluiApp(model: model),
    ),
  );
  unawaited(channel.invokeMethod<void>('ready', 1));
}
