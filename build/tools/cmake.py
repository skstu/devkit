#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import sys
import subprocess


def cmake_configure(input_options):
    options = ""
    if len(input_options) > 0:
        options += ' '.join(map(lambda item: item, input_options.split(',')))
        config_cmd = 'cmake %s' % (options)
        subprocess.run(config_cmd, shell=True)


def cmake_build(input_options):
    options = ""
    if len(input_options) > 0:
        options += ' '.join(map(lambda item: item, input_options.split(',')))
        build_cmd = 'cmake %s' % (options)
        subprocess.run(build_cmd, shell=True)


def cmake_install(input_options):
    options = ""
    if len(input_options) > 0:
        options += ' '.join(map(lambda item: item, input_options.split(',')))
        install_cmd = 'cmake %s' % (options)
        subprocess.run(install_cmd, shell=True)


def main():
    [
        cmake_options_configure,
        cmake_options_build,
        cmake_options_install,
    ] = sys.argv[1:]

    cmake_configure(cmake_options_configure.strip())
    cmake_build(cmake_options_build.strip())
    cmake_install(cmake_options_install.strip())


if __name__ == '__main__':
    main()
