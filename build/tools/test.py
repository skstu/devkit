import os
import sys
import platform
import subprocess

# 定义msys2_shell.cmd的路径
msys2_shell_path = "C:/msys64/msys2_shell.cmd"

# 要执行的命令列表


def main():
    import subprocess

    # 定义变量
    # source_dir = "/C/Users/k34ub/source/Projects/3rdparty/x264"
    source_dir = "/C/Users/k34ub/source/Projects/3rdparty/ffmpeg"
    msys2_shell_path = r'C:\msys64\msys2_shell.cmd'
    CC_PATH = "/C/msys64/clang64/bin"
    MSYS_PATH = "/C/msys64/usr/bin"
    execmd = f'{msys2_shell_path} -defterm -msys -here -no-start -c "cd {source_dir} && make clean && export PATH=$PATH:{CC_PATH} && CC=clang && bash ./configure --enable-static"'
    # 构建命令列表
    # -defterm -no-start -mingw64 -here -c "your command here"
    subprocess.run(execmd)

    # commands = [
    #   f'cd {source_dir} && export PATH="{CC_PATH}:$PATH" && CC=clang && ./configure --enable-static'
    # ]

    # 调用msys2_shell.cmd并执行命令
    # result = subprocess.run([msys2_shell_path, '-u', '-c'] + commands,
    #                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # print(result.stderr.decode('utf-8'))
    # print(result.stdout.decode('utf-8'))

    # source_dir = "/C/Users/k34ub/source/Projects/3rdparty/x264"
    # commands = [f'cd {source_dir}', f'CC=clang',
    #           f'./configure --enable-static']
    # , creationflags=subprocess.CREATE_NO_WINDOW
    # 将命令列表作为一个字符串传递给msys2_shell.cmd，并保持控制台不退出
    # cmds_final = [msys2_shell_path, '-defterm', '-mingw64',
    #              '--norxvt', '-c'] + [' && '.join(commands)]
    # exec_cmd = f'{msys2_shell_path} -c cd {source_dir} && CC=clang && ./configure --enable-static'
    # cmds_final = [msys2_shell_path, '-c'] + [' && '.join(commands)]
    # print(exec_cmd)
    # subprocess.run(exec_cmd,
    #               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)


if __name__ == '__main__':
    main()
