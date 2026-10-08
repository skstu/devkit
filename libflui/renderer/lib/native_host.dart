import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:xml/xml.dart';

import 'retained.dart';

final class Span extends Struct {
  external Pointer<Utf8> data;
  @Uint64()
  external int size;
}

typedef Visitor = Int32 Function(Uint32, Span, Span, Pointer<Void>);
typedef Parse = Int32 Function(
  Pointer<Utf8>,
  Uint64,
  Pointer<NativeFunction<Visitor>>,
  Pointer<Void>,
);
typedef Attach = Int32 Function(
  Pointer<NativeFunction<Void Function()>>,
  Pointer<NativeFunction<Parse>>,
);
typedef Event = Void Function(
  Uint64,
  Uint32,
  Uint64,
  Int32,
  Uint64,
  Pointer<Utf8>,
  Pointer<Utf8>,
);
typedef Invoke = Void Function(Uint64, Uint32);

class NativeHost {
  late final DynamicLibrary library;
  late final NativeCallable<Void Function()> wake;
  late final NativeCallable<Parse> parser;
  late final void Function(
    int,
    int,
    int,
    int,
    int,
    Pointer<Utf8>,
    Pointer<Utf8>,
  )
  event;
  late final void Function(int, int) invoke;
  late final Pointer<Utf8> Function() take;
  late final RetainedModel model;
  final timers = <int, Timer>{};
  int window = 0;
  bool draining = false;
  NativeHost() {
    final name = Platform.isWindows
        ? 'flui.dll'
        : Platform.isIOS
        ? 'flui.framework/flui'
        : Platform.isLinux
        ? '${File(Platform.resolvedExecutable).parent.path}/lib/libflui.so'
        : 'libflui.so';
    library = DynamicLibrary.open(name);
    event = library
        .lookupFunction<
          Event,
          void Function(int, int, int, int, int, Pointer<Utf8>, Pointer<Utf8>)
        >('flui_host_event');
    invoke = library.lookupFunction<Invoke, void Function(int, int)>(
      'flui_host_invoke',
    );
    take = library
        .lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>(
          'flui_host_take',
        );
    model = RetainedModel((name, value) => emit(2, name: name, value: value));
    wake = NativeCallable<Void Function()>.listener(drain);
    parser = NativeCallable<Parse>.isolateLocal(parse, exceptionalReturn: 8);
    final attach = library
        .lookupFunction<
          Attach,
          int Function(
            Pointer<NativeFunction<Void Function()>>,
            Pointer<NativeFunction<Parse>>,
          )
        >('flui_host_attach');
    if (attach(wake.nativeFunction, parser.nativeFunction) != 0)
      throw StateError('libflui host attach failed');
  }
  void emit(
    int kind, {
    int request = 0,
    int status = 0,
    int bytes = 0,
    String name = '',
    String value = '',
  }) {
    final n = name.toNativeUtf8(), v = value.toNativeUtf8();
    try {
      event(window, kind, request, status, bytes, n, v);
    } finally {
      calloc.free(n);
      calloc.free(v);
    }
  }

  int parse(
    Pointer<Utf8> input,
    int length,
    Pointer<NativeFunction<Visitor>> callback,
    Pointer<Void> user,
  ) {
    final xml = input.toDartString(length: length);
    if (xml.contains('<!DOCTYPE') || xml.contains('<!ENTITY')) return 8;
    final document = XmlDocument.parse(xml);
    final visit = callback
        .asFunction<int Function(int, Span, Span, Pointer<Void>)>();
    var nodes = 0;
    void call(int kind, String name, String value) {
      final n = name.toNativeUtf8(), v = value.toNativeUtf8();
      final ns = calloc<Span>(), vs = calloc<Span>();
      ns.ref
        ..data = n
        ..size = utf8.encode(name).length;
      vs.ref
        ..data = v
        ..size = utf8.encode(value).length;
      try {
        if (visit(kind, ns.ref, vs.ref, user) != 0)
          throw const FormatException('XML visitor aborted');
      } finally {
        calloc.free(ns);
        calloc.free(vs);
        calloc.free(n);
        calloc.free(v);
      }
    }

    void walk(XmlElement e, int depth) {
      if (depth > 64 || ++nodes > 10000)
        throw const FormatException('XML limit');
      call(1, e.name.qualified, '');
      for (final a in e.attributes) {
        call(2, a.name.qualified, a.value);
      }
      for (final c in e.childElements) {
        walk(c, depth + 1);
      }
      call(3, e.name.qualified, '');
    }

    walk(document.rootElement, 0);
    return 0;
  }

  void start() {
    final name = Platform.isWindows
        ? 'flui_client.dll'
        : Platform.isIOS
        ? 'FluiClient.framework/FluiClient'
        : Platform.isLinux
        ? '${File(Platform.resolvedExecutable).parent.path}/lib/libflui_client.so'
        : 'libflui_client.so';
    final client = DynamicLibrary.open(name);
    final start = client
        .lookupFunction<Int32 Function(Uint32), int Function(int)>(
          'flui_client_start',
        );
    if (start(1) != 0) throw StateError('Consumer startup failed');
    drain();
  }

  void drain() {
    if (draining) return;
    draining = true;
    try {
      // Drain a bounded batch; notifications continue work without an idle poll.
      for (var i = 0; i < 128; i++) {
        final p = take();
        if (p == nullptr) break;
        final cmd = jsonDecode(p.toDartString()) as Map<String, dynamic>;
        final op = cmd['op'];
        if (op == 'create') {
          window = cmd['window'] as int;
          emit(1);
        } else if (op == 'dispatch') {
          invoke(cmd['id'] as int, 1);
        } else if (op == 'timer') {
          final id = cmd['id'] as int, ms = cmd['ms'] as int;
          timers.remove(id)?.cancel();
          if (ms > 0)
            timers[id] = Timer.periodic(
              Duration(milliseconds: ms),
              (_) => invoke(id, 0),
            );
        } else if (op == 'close') {
          emit(4);
          for (final t in timers.values) {
            t.cancel();
          }
          timers.clear();
        } else {
          var status = 0, reason = '';
          try {
            if (op == 'tree')
              model.setTree(cmd['data'] as String);
            else if (op == 'patch')
              model.patch(cmd['data'] as String);
            else
              throw const FormatException(
                'This host supports retained documents',
              );
          } catch (e) {
            status = 8;
            reason = '$e';
          }
          emit(
            3,
            request: cmd['request'] as int,
            status: status,
            bytes: cmd['bytes'] as int,
            value: reason,
          );
        }
      }
    } finally {
      draining = false;
    }
  }
}

void runNativeHost() {
  WidgetsFlutterBinding.ensureInitialized();
  final host = NativeHost();
  FlutterError.onError = (details) {
    FlutterError.presentError(details);
    host.emit(5, status: 7, value: details.exceptionAsString());
  };
  runApp(RetainedApp(model: host.model));
  host.start();
}
