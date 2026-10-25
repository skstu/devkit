import 'dart:io';
import 'package:devkit_flui_renderer/file_actions.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final messenger = TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
  tearDown(() {
    debugDefaultTargetPlatformOverride = null;
    messenger.setMockMethodCallHandler(fileActionsChannel, null);
  });
  for (final platform in [TargetPlatform.iOS, TargetPlatform.android]) {
    test('$platform delegates view and export with the logical filename', () async {
      debugDefaultTargetPlatformOverride = platform;
      final calls = <MethodCall>[];
      messenger.setMockMethodCallHandler(fileActionsChannel, (call) async {
        calls.add(call);
        return {'status': 0, 'path': 'file:///selected/photo.png'};
      });
      final names = ['photo.png', '应用.APK', 'setup.exe', 'disk.img'];
      for (final name in names) for (final kind in [4, 5]) {
        final result = await localFileAction({'kind': kind, 'source': '/private/fixture', 'initialName': name});
        expect(result, (status: 0, path: 'file:///selected/photo.png'));
      }
      expect(calls.map((call) => call.method), [for (final _ in names) ...['view', 'export']]);
      expect(calls.map((call) => call.arguments['name']), [for (final name in names) ...[name, name]]);
      expect(calls.every((call) => call.arguments['source'] == '/private/fixture'), isTrue);
    });
  }
  test('repeat preview reuses a copy; distinct sources and changed bytes remain separate', () async {
    final root=await Directory.systemTemp.createTemp('flui-preview-test-');
    final copies=<String>{};
    try {
      final source=await File('${root.path}/one').writeAsBytes([1,2,3]);
      final other=await File('${root.path}/two').writeAsBytes([4,5]);
      final first=await previewCopy(source.path,'photo.png');copies.add(first);
      expect(await previewCopy(source.path,'photo.png'),first);
      await File(first).writeAsBytes([0,0,0,0,0]); // A viewer edited its disposable copy.
      final unedited=await previewCopy(source.path,'photo.png');copies.add(unedited);
      expect(unedited,isNot(first));expect(await File(unedited).readAsBytes(),[1,2,3]);
      final second=await previewCopy(other.path,'photo.png');copies.add(second);
      expect(second,isNot(first));
      await source.writeAsBytes([6,7,8,9]);
      final changed=await previewCopy(source.path,'photo.png');copies.add(changed);
      expect(changed,isNot(first));expect(await File(changed).readAsBytes(),[6,7,8,9]);
      await File(changed).delete();
      final replaced=await previewCopy(source.path,'photo.png');copies.add(replaced);
      expect(replaced,isNot(changed));expect(await source.readAsBytes(),[6,7,8,9]);
    } finally {
      for(final path in copies) { final parent=File(path).parent;if(await parent.exists())await parent.delete(recursive:true); }
      await root.delete(recursive:true);
    }
  });
  test('folder copy retains nesting, empty directories and bytes; rejects links', () async {
    final root=await Directory.systemTemp.createTemp('flui-folder-test-');
    try {
      final source=await Directory('${root.path}/source').create();
      await Directory('${source.path}/空目录').create();
      await Directory('${source.path}/nested').create();
      final bytes=List.generate(1024,(i)=>i%251);
      await File('${source.path}/nested/照片.bin').writeAsBytes(bytes);
      await copyLocalObject(source.path,'${root.path}/copy');
      expect(await File('${root.path}/copy/nested/照片.bin').readAsBytes(),bytes);
      expect(await Directory('${root.path}/copy/空目录').exists(),isTrue);
      await Link('${source.path}/link').create('${source.path}/nested/照片.bin');
      await expectLater(copyLocalObject(source.path,'${root.path}/rejected'),throwsA(isA<FileSystemException>()));
    } finally {await root.delete(recursive:true);}
  });
  test('photo chooser preserves selection, cancellation and errors', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    for (final reply in [{'status': 0, 'path': '/cache/photo.heic'}, {'status': 0, 'path': ''}, {'status': 7, 'path': ''}]) {
      messenger.setMockMethodCallHandler(fileActionsChannel, (call) async {
        expect(call.method, 'pick_media');expect(call.arguments, isNull);return reply;
      });
      expect(await pickMedia(), (status: reply['status'], path: reply['path']));
    }
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    expect(await pickMedia(), (status: 10, path: ''));
  });
  test('Android file selection delegates paths without reading file bytes', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    for (final reply in [{'status': 0, 'path': '/cache/large.mp4'}, {'status': 0, 'path': ''}, {'status': 7, 'path': ''}]) {
      messenger.setMockMethodCallHandler(fileActionsChannel, (call) async {
        expect(call.method, 'pick_file');expect(call.arguments, isNull);return reply;
      });
      expect(await pickLocalFile(), (status: reply['status'], path: reply['path']));
    }
  });
  test('Android media preserves native cancellation, ordered selections and limit errors', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    for (final multiple in [false, true]) {
      final path=multiple?'["/cache/one.png","/cache/large.mp4"]':'/cache/large.mp4';
      for(final reply in [{'status': 0, 'path': path}, {'status': 0, 'path': ''}, {'status': 1, 'path': ''}, {'status': 7, 'path': ''}]) {
        messenger.setMockMethodCallHandler(fileActionsChannel, (call) async {
          expect(call.method, 'pick_media');expect(call.arguments, multiple?{'multiple':true}:null);return reply;
        });
        expect(await pickMedia(multiple: multiple),(status:reply['status'],path:reply['path']));
      }
    }
  });
  test('iOS media selection keeps its existing native interface', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    messenger.setMockMethodCallHandler(fileActionsChannel, (call) async {
      expect(call.method, 'pick_media');expect(call.arguments, {'multiple': true});
      return {'status': 0, 'path': '["/cache/one.heic","/cache/two.heic"]'};
    });
    expect(await pickMedia(multiple: true), (status: 0, path: '["/cache/one.heic","/cache/two.heic"]'));
  });
  test('iOS cancellation and native errors remain distinct from success', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    for (final status in [0, 6, 7, 10]) {
      messenger.setMockMethodCallHandler(fileActionsChannel, (_) async => {'status': status, 'path': ''});
      expect(await localFileAction({'kind': 5, 'source': '/fixture', 'initialName': 'photo.png'}), (status: status, path: ''));
    }
  });
}
