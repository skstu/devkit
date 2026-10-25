import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

// Destination selection and copy completion are distinct from transfer state.
const fileActionsChannel = MethodChannel('devkit.libflui/files');
Future<({int status, String path})> localFileAction(Map<String,dynamic> command) async {
  final action=command['kind'] as int, source=command['source'] as String;
  final name=command['initialName'] as String;
  if(defaultTargetPlatform==TargetPlatform.android || defaultTargetPlatform==TargetPlatform.iOS) {
    final result=await fileActionsChannel.invokeMapMethod<String,dynamic>(
      action==4?'view':'export', {'source':source,'name':name});
    return (status:result?['status'] as int? ?? 7,path:result?['path'] as String? ?? '');
  }
  final type=await FileSystemEntity.type(source,followLinks:false);
  if(type!=FileSystemEntityType.file && type!=FileSystemEntityType.directory)return (status:7,path:'');
  final directorySource=type==FileSystemEntityType.directory;
  String target;
  if(action==4) {
    target=await previewCopy(source,name);
  } else {
    if(directorySource) {
      final parent=await getDirectoryPath(confirmButtonText:command['title'] as String);
      if(parent==null)return (status:0,path:'');
      target='$parent${Platform.pathSeparator}$name';
    } else {
      final selection=await getSaveLocation(suggestedName:name,confirmButtonText:command['title'] as String);
      if(selection==null)return (status:0,path:'');
      target=selection.path;
    }
    if(File(source).absolute.path==File(target).absolute.path)return (status:7,path:'');
  }
  if(directorySource && action!=4) {
    if(await FileSystemEntity.type(target,followLinks:false)!=FileSystemEntityType.notFound)return (status:7,path:'');
    final original=await Directory(source).resolveSymbolicLinks();
    final parent=await File(target).parent.resolveSymbolicLinks();
    if(parent==original || parent.startsWith('$original${Platform.pathSeparator}'))return (status:7,path:'');
  }
  if(action!=4) {
    final temp=await File(target).parent.createTemp('.libflui-');
    final staging=File('${temp.path}${Platform.pathSeparator}export');
    try {
      await copyLocalObject(source,staging.path);
      if(directorySource) {
        if(await FileSystemEntity.type(target,followLinks:false)!=FileSystemEntityType.notFound)return (status:7,path:'');
        await Directory(staging.path).rename(target);
      } else await staging.rename(target);
    } finally { await temp.delete(recursive:true); }
  }
  if(action==4) {
    final result=Platform.isWindows?await Process.run('explorer.exe',[target])
                                  :await Process.run('xdg-open',[target]);
    if(result.exitCode!=0)return (status:7,path:'');
  }
  return (status:0,path:target);
}

// System chooser owns library access; return only the explicitly selected copy.
Future<({int status, String path})> pickLocalFile() async {
  if (defaultTargetPlatform != TargetPlatform.android) {
    return (status: 0, path: (await openFile())?.path ?? '');
  }
  final result = await fileActionsChannel.invokeMapMethod<String, dynamic>('pick_file');
  return (status: result?['status'] as int? ?? 7, path: result?['path'] as String? ?? '');
}

Future<({int status, String path})> pickMedia({bool multiple = false}) async {
  if (defaultTargetPlatform == TargetPlatform.android) {
    // file_selector_android returns the entire file as a platform-channel byte
    // array. Our native importer streams to disk and sends paths only.
    final result = await fileActionsChannel.invokeMapMethod<String, dynamic>(
      'pick_media', multiple ? {'multiple': true} : null);
    return (status: result?['status'] as int? ?? 7, path: result?['path'] as String? ?? '');
  }
  if (defaultTargetPlatform != TargetPlatform.iOS) return (status: 10, path: '');
  final result = await fileActionsChannel.invokeMapMethod<String, dynamic>(
    'pick_media', multiple ? {'multiple': true} : null);
  return (status: result?['status'] as int? ?? 7, path: result?['path'] as String? ?? '');
}

// Copy a selected/received local tree without following links or loading files
// into memory. Publishing the completed staging tree is owned by the caller.
Future<void> copyLocalObject(String source,String target) async {
  final type=await FileSystemEntity.type(source,followLinks:false);
  if(type==FileSystemEntityType.file) {await File(source).copy(target);return;}
  if(type!=FileSystemEntityType.directory)throw FileSystemException('Unsupported local object');
  await Directory(target).create();
  await for(final entry in Directory(source).list(followLinks:false)) {
    final name=entry.path.split(Platform.pathSeparator).last;
    await copyLocalObject(entry.path,'$target${Platform.pathSeparator}$name');
  }
}

// One URL per unchanged source/name in this process; do not reuse a filename
// for a different sender's file. A removed cache copy is recreated on demand.
final _previewCopies=<(String,String,int,DateTime),({String path,int size,DateTime modified})>{};
Future<String> previewCopy(String source,String name) async {
  final stat=await FileStat.stat(source);
  final key=(File(source).absolute.path,name,stat.size,stat.modified);
  final existing=stat.type==FileSystemEntityType.file?_previewCopies[key]:null;
  if(existing!=null) {
    final copied=await FileStat.stat(existing.path);
    if(copied.type==FileSystemEntityType.file && copied.size==existing.size && copied.modified==existing.modified)return existing.path;
  }
  final directory=await Directory.systemTemp.createTemp('libflui-open-');
  final target='${directory.path}${Platform.pathSeparator}$name';
  try { await copyLocalObject(source,target); }
  catch(_) { await directory.delete(recursive:true);rethrow; }
  if(stat.type==FileSystemEntityType.file) {
    final copied=await FileStat.stat(target);
    _previewCopies[key]=(path:target,size:copied.size,modified:copied.modified);
  }
  return target;
}
