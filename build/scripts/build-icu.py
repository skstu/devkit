#!/usr/bin/python
# -*- coding: utf-8 -*-
import os
import re
import sys
import platform
import subprocess
import threading
import xml.etree.ElementTree as ET
sys.dont_write_bytecode = True
sys.stdout.reconfigure(encoding='utf-8')

project_root = os.path.dirname(os.path.abspath(__file__))+"/../../"
script_py387 = os.path.join(project_root, "include/python", "")
sys.path.append(script_py387)
import py387

sys_path_original = sys.path.copy()
sys.path = sys_path_original
platform_system = platform.system().lower()

icu_source_dir = project_root + r"third_party/icu/"

setup_config_xml = r'''
<CompileConfig>
 <VisualStudio
  Version="2019"
  InstallRoot="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\">
 </VisualStudio>
</CompileConfig>
'''

'''
def _GetCompileConfigVSInstallRoot(filePathname):
  compile_config_xml_tree = ET.parse(filePathname)
  compile_config_xml_root = compile_config_xml_tree.getroot()
  vs_version = compile_config_xml_root.find(".//VisualStudio").attrib["Version"]
  vs_install_root = compile_config_xml_root.find(".//VisualStudio").attrib["InstallRoot"]
  return vs_version,vs_install_root
'''


def _GetCompileConfigVSInstallRoot(xml_buffer):
    compile_config_xml_tree = ET.fromstring(xml_buffer)
    compile_config_xml_root = compile_config_xml_tree  # No need to call getroot() here
    vs_version = compile_config_xml_root.find(
        ".//VisualStudio").attrib["Version"]
    vs_install_root = compile_config_xml_root.find(
        ".//VisualStudio").attrib["InstallRoot"]
    return vs_version, vs_install_root


def _GetVS_vcvarsall_x86():
    vs_version, vs_install_root = _GetCompileConfigVSInstallRoot(
        setup_config_xml)
    return vs_install_root + r'\VC\Auxiliary\Build\vcvars32.bat'


def _GetVS_vcvarsall_x64():
    vs_version, vs_install_root = _GetCompileConfigVSInstallRoot(
        setup_config_xml)
    return vs_install_root + r'\VC\Auxiliary\Build\vcvars64.bat'


def main():
    result = False
    specify_icu_version = "75.0.1"
    source_build_dir = icu_source_dir + r'{}/icu4c/source'.format(specify_icu_version)
    install_dir = project_root + r'bin/third_party/icu/{}/{}'.format(specify_icu_version, platform_system)

    if not os.path.exists(source_build_dir):
        return result
    if platform_system == 'linux':
        subprocess.run(f'cd {source_build_dir} && \
      make clean && \
      ./runConfigureICU Linux -prefix="{install_dir}/Release" \
      -enable-static -disable-shared --disable-renaming &&\
      make -j8 && make install && make clean', shell=True)

        subprocess.run(f'cd {source_build_dir} && \
      make clean && \
      ./runConfigureICU --enable-debug --disable-release \
        Linux -prefix="{install_dir}/Debug" \
      -enable-static -disable-shared --disable-renaming &&\
        make -j8 && make install && make clean', shell=True)
 
        result = True
    elif platform_system == 'windows':
        use_msvc = False
        # vs_vcvarsall_x86 = _GetVS_vcvarsall_x86()
        vs_vcvarsall_x64 = _GetVS_vcvarsall_x64()

        install_dir = "/" + os.path.normpath(install_dir).replace("\\", "/")[0:]
        install_dir = install_dir.replace(":","")

        if(use_msvc) :
            install_dir+="/msvc"
            subprocess.run(f'"{vs_vcvarsall_x64}" && \
            cd {source_build_dir} && \
            make clean && \
            bash runConfigureICU Cygwin/MSVC -prefix="{install_dir}/Release" \
            -enable-static -disable-shared --disable-renaming && \
            cd config && \
            del mh-unknown && copy mh-msys-msvc mh-unknown && \
            cd .. && \
            make -j8 && make install && make clean')

            subprocess.run(f'"{vs_vcvarsall_x64}" && \
            cd {source_build_dir} && \
            make clean && \
            bash runConfigureICU --enable-debug --disable-release \
            Cygwin/MSVC -prefix="{install_dir}/Debug" \
            -enable-static -disable-shared --disable-renaming && \
            cd config && \
            del mh-unknown && copy mh-msys-msvc mh-unknown && \
            cd .. && \
            make -j8 && make install && make clean')
        else:
            install_dir+="/mingw"
            subprocess.run(f'cmd.exe /c cd {source_build_dir} && \
            make clean && \
            bash runConfigureICU MinGW -prefix="{install_dir}/Release" \
            -enable-static -disable-shared --disable-renaming && \
            cd config && \
            del mh-unknown && copy mh-cygwin64 mh-unknown && \
            cd .. && \
            make -j8 && make install && make clean')

            subprocess.run(f'cmd.exe /c cd {source_build_dir} && \
            make clean && \
            bash runConfigureICU --enable-debug --disable-release \
            MinGW -prefix="{install_dir}/Debug" \
            -enable-static -disable-shared --disable-renaming&& \
            cd config && \
            del mh-unknown && copy mh-cygwin64 mh-unknown && \
            cd .. && \
            make -j8 && make install && make clean')

        result = True
    return result


if __name__ == '__main__':
    main()
