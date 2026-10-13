import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:devkit_flui_renderer/retained.dart';

Map<String,Object> n(String id,String tag,Map<String,String> attrs,[List<Map<String,Object>> children=const []])=>{'id':id,'tag':tag,'attrs':attrs,'children':children};
void main(){
  for(final text in ['hello','中文消息 👩‍👩‍👧‍👦','long message '*30]){
    testWidgets('intrinsic text bubble has final geometry on first paint: $text',(tester) async {
      final events=<String>[];final model=RetainedModel((event,value){events.add(event);});
      final status={'text':'queued','width':'1','height':'20'};
      model.setTree(jsonEncode(n('1','Window',{},[
        n('2','List',{'showheader':'false'},[
          n('3','HorizontalLayout',{'autoheight':'true','height':'100','inset':'24,10,24,6','childpadding':'8'},[
            n('4','Control',{}),
            n('5','VerticalLayout',{'autoheight':'true','autowidth':'true','maxwidth':'220','childpadding':'6'},[
              n('6','VerticalLayout',{'autoheight':'true','inset':'12,8,12,8','bkcolor':'#90ee90'},[
                n('7','Label',{'text':text,'wordwrap':'true','autoheight':'true','fontsize':'16','emojiscale':'1.4'})]),
              n('8','Label',status)]),
            n('9','VerticalLayout',{'width':'36'},[n('10','Control',{'height':'36'}),n('11','Control',{})])])])])));
      await tester.pumpWidget(RetainedApp(model:model));
      expect(tester.takeException(),isNull);
      final bubble=find.byKey(const ValueKey('6'));
      final first=tester.getRect(bubble);
      expect(first.width,greaterThan(24));expect(first.width,lessThanOrEqualTo(220));
      await tester.pump();expect(tester.getRect(bubble),first);
      model.patch(jsonEncode([{'id':'8','attrs':{...status,'text':'delivered and persisted'}}]));
      await tester.pump();expect(tester.getRect(bubble),first);
      expect(events.where((e)=>e=='textwidth'||e=='textmeasure'),isEmpty);
      expect(tester.takeException(),isNull);
      await tester.pumpWidget(const SizedBox());
    });
  }
  testWidgets('fixed discovery heading survives content scrolling',(tester) async {
    final model=RetainedModel((_,__){});
    model.setTree(jsonEncode(n('1','Window',{},[n('12','VerticalLayout',{},[
      n('13','HorizontalLayout',{'height':'64'},[n('14','Button',{'width':'48','text':'Back'}),n('15','Label',{'text':'LAN discovery'})]),
      n('16','ScrollLayout',{},[for(var i=0;i<20;i++)n('${100+i}','Label',{'text':'Device $i','height':'88'})])])])));
    await tester.pumpWidget(RetainedApp(model:model));
    final header=find.byKey(const ValueKey('13'));final before=tester.getRect(header);
    await tester.drag(find.byType(SingleChildScrollView),const Offset(0,-400));await tester.pumpAndSettle();
    expect(tester.getRect(header),before);expect(find.text('Back',findRichText:true),findsOneWidget);expect(tester.takeException(),isNull);
    await tester.pumpWidget(const SizedBox());
  });
}
