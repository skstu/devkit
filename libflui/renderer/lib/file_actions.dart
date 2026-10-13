import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/services.dart';

// Destination selection and copy completion are distinct from transfer state.
const fileActionsChannel = MethodChannel('devkit.libflui/files');
Future<({int status, String path})> localFileAction(Map<String,dynamic> command) async {
  final action=command['kind'] as int, source=command['source'] as String;
  final name=command['initialName'] as String;
  if(Platform.isAndroid) {
    final result=await fileActionsChannel.invokeMapMethod<String,dynamic>(
      action==4?'view':'export', {'source':source,'name':name});
    return (status:result?['status'] as int? ?? 7,path:result?['path'] as String? ?? '');
  }
  if(Platform.isIOS)return (status:10,path:''); // Document export adapter not validated yet.
  String target;
  if(action==4) {
    final directory=await Directory.systemTemp.createTemp('libflui-open-');
    target='${directory.path}${Platform.pathSeparator}$name';
  } else {
    final selection=await getSaveLocation(suggestedName:name,confirmButtonText:command['title'] as String);
    if(selection==null)return (status:0,path:'');
    target=selection.path;
    if(File(source).absolute.path==File(target).absolute.path)return (status:7,path:'');
  }
  final temp=await File(target).parent.createTemp('.libflui-');
  final staging=File('${temp.path}${Platform.pathSeparator}export');
  try {
    await File(source).copy(staging.path);
    await staging.rename(target);
  } finally { await temp.delete(recursive:true); }
  if(action==4) {
    final result=Platform.isWindows?await Process.run('explorer.exe',[target])
                                  :await Process.run('xdg-open',[target]);
    if(result.exitCode!=0)return (status:7,path:'');
  }
  return (status:0,path:target);
}
