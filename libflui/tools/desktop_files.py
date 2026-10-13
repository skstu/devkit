"""Install the SDK's OS file-drop adapters in the generated desktop runner."""
from pathlib import Path
import shutil

def install(host: Path, platform: str):
    component=Path(__file__).resolve().parent.parent
    runner=host/platform/'runner'
    def replace(text,old,new):
        if text.count(old)!=1: raise ValueError(f'Desktop runner template changed: {old}')
        return text.replace(old,new)
    if platform=='windows':
        header=runner/'flutter_window.h';source=runner/'flutter_window.cpp'
        h=replace(header.read_text(),'#include "win32_window.h"','#include "win32_window.h"\n#include "flui_desktop_files.h"')
        h=replace(h,'flutter_controller_;','flutter_controller_;\n  std::unique_ptr<FluiDesktopFiles> desktop_files_;')
        s=replace(source.read_text(),'  SetChildContent(', '  desktop_files_=std::make_unique<FluiDesktopFiles>(GetHandle(),flutter_controller_->engine()->messenger());\n  SetChildContent(')
        s=replace(s,'void FlutterWindow::OnDestroy() {','void FlutterWindow::OnDestroy() {\n  desktop_files_.reset();')
        s=replace(s,'  // Give Flutter, including plugins,','  if(message==WM_DROPFILES && desktop_files_) {desktop_files_->Drop(reinterpret_cast<HDROP>(wparam));return 0;}\n  // Give Flutter, including plugins,')
        header.write_text(h);source.write_text(s)
    elif platform=='linux':
        source=runner/'my_application.cc'
        s=replace(source.read_text(),'#include "my_application.h"','#include "my_application.h"\n#include "flui_desktop_files.h"')
        s=replace(s,'  fl_register_plugins(FL_PLUGIN_REGISTRY(view));','  fl_register_plugins(FL_PLUGIN_REGISTRY(view));\n  flui_desktop_files(view);')
        source.write_text(s)
    else: raise ValueError(platform)
    shutil.copy2(component/'platform'/platform/'flui_desktop_files.h',runner/'flui_desktop_files.h')
