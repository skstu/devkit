#!/usr/bin/python
# -*- coding: utf-8 -*-
import py387
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
sys_path_original = sys.path.copy()
sys.path = sys_path_original
platform_system = platform.system().lower()

source_dir = project_root + r"3rdparty/libuv/"


def main():
    result = False
    print("asf")
    return result


if __name__ == '__main__':
    main()
