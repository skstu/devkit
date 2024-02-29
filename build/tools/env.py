import os
import re
import sys
import platform
import subprocess

platform_system = platform.system().lower()


def GetCurrentRootDir():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir += "\\..\\..\\"
    root_dir = os.path.normpath(root_dir)
    root_dir = root_dir.replace("\\", "/")
    return root_dir


def GetCurrentRootDirPosixStyle():
    root_dir = os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", ".."))
    if not root_dir.startswith('/') and not root_dir.startswith('\\'):
        root_dir = "/"+root_dir
    root_dir = os.path.normpath(root_dir)
    root_dir = root_dir.replace('\\', '/')
    root_dir = root_dir.replace(':', '')
    return root_dir


def main():
    (input_arg1,) = sys.argv[1:]
    root_dir = GetCurrentRootDir()
    root_dir_posix_style = GetCurrentRootDirPosixStyle()
    print('ROOT_DIR ="%s"' % root_dir)
    print('ROOT_DIR_POSIX_STYLE ="%s"' % root_dir_posix_style)


if __name__ == '__main__':
    main()
