import os
import sys
import platform
import subprocess

platform_system_name = platform.system().lower()


def __format(script_content):
    return (' '.join(map(lambda item: item,
                         script_content.split(',')))).strip()


def __execute(terminal, script_content):
    exec_sciprt = script_content
    if len(terminal) > 0:
        exec_sciprt = f'{terminal} "{script_content}"'
    # print(exec_sciprt)
    subprocess.run(exec_sciprt, shell=True)
    return


def cmd_clean(terminal, script_content):
    __execute(terminal, script_content)
    return


def cmd_configure(terminal, script_content):
    __execute(terminal, script_content)
    return


def cmd_install(terminal, script_content):
    __execute(terminal, script_content)
    return


def cmd_build(terminal, script_content):
    __execute(terminal, script_content)
    return


def main():
    [
        terminal,
        script_configure,
        script_build,
        script_install,
        script_clean,
    ] = sys.argv[1:]
    terminal = __format(terminal)
    script_clean = __format(script_clean)
    cmd_clean(terminal, script_clean)
    cmd_configure(terminal, __format(script_configure))
    cmd_build(terminal, __format(script_build))
    cmd_install(terminal, __format(script_install))
    cmd_clean(terminal, script_clean)
    return


if __name__ == '__main__':
    main()
