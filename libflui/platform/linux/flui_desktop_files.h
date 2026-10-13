#pragma once
#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <cstring>

static void flui_desktop_files(FlView* view) {
  g_autoptr(FlStandardMethodCodec) codec=fl_standard_method_codec_new();
  FlMethodChannel* channel=fl_method_channel_new(fl_engine_get_binary_messenger(fl_view_get_engine(view)),
      "devkit.libflui/desktop_files",FL_METHOD_CODEC(codec));
  g_object_set_data_full(G_OBJECT(view),"flui-desktop-files",channel,g_object_unref);
  fl_method_channel_set_method_call_handler(channel,[](FlMethodChannel*,FlMethodCall* call,gpointer data) {
    auto* widget=GTK_WIDGET(data);
    if(std::strcmp(fl_method_call_get_name(call),"set_enabled")!=0) {
      fl_method_call_respond_not_implemented(call,nullptr);return;
    }
    FlValue* args=fl_method_call_get_args(call);
    if(fl_value_get_type(args)!=FL_VALUE_TYPE_BOOL) {
      fl_method_call_respond_error(call,"argument","Expected bool",nullptr,nullptr);return;
    }
    if(fl_value_get_bool(args)) {
      GtkTargetEntry target{const_cast<gchar*>("text/uri-list"),0,0};
      gtk_drag_dest_set(widget,GTK_DEST_DEFAULT_ALL,&target,1,GDK_ACTION_COPY);
    } else gtk_drag_dest_unset(widget);
    fl_method_call_respond_success(call,nullptr,nullptr);
  },view,nullptr);
  g_signal_connect(view,"drag-data-received",G_CALLBACK(+[](GtkWidget*,GdkDragContext*,gint,gint,
      GtkSelectionData* selection,guint,guint,gpointer data) {
    gchar** uris=gtk_selection_data_get_uris(selection);
    g_autoptr(FlValue) paths=fl_value_new_list();
    bool valid=uris && g_strv_length(uris)<=32;size_t bytes=0;
    if(valid)for(gchar** p=uris;*p;++p) {
      gchar* host=nullptr;
      gchar* filename=g_filename_from_uri(*p,&host,nullptr);
      gchar* utf8=filename?g_filename_to_utf8(filename,-1,nullptr,nullptr,nullptr):nullptr;
      valid=utf8 && g_path_is_absolute(filename) && (!host || !*host || std::strcmp(host,"localhost")==0);
      if(valid)valid=(bytes+=std::strlen(utf8))<=60000;
      if(valid)fl_value_append_take(paths,fl_value_new_string(utf8));
      g_free(utf8);g_free(filename);g_free(host);
      if(!valid)break;
    }
    g_strfreev(uris);
    if(!valid){fl_value_unref(paths);paths=fl_value_new_list();}
    fl_method_channel_invoke_method(FL_METHOD_CHANNEL(data),"drop",paths,nullptr,nullptr,nullptr);
  }),channel);
}
