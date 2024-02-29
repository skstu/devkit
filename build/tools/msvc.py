import os
import sys
import locale
import subprocess
import re

VC_60_VERSION = 60
VS_2002_VERSION = 70
VS_2003_VERSION = 71
VS_2005_VERSION = 80
VS_2008_VERSION = 90
VS_2010_VERSION = 100
VS_2012_VERSION = 110
VS_2013_VERSION = 120
VS_2015_VERSION = 140
VS_2017_VERSION = 150
VS_2019_VERSION = 160
VS_2022_VERSION = 170


def ExecuteCmd(cmd):
    encoding = locale.getpreferredencoding(False)
    (stdoutdata, stderrdata) = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    ).communicate()
    if stderrdata is not None:
        print(stderrdata.decode(encoding))
        return False
    output = stdoutdata.decode(encoding)
    return output.strip()


def DetectSetEnvBatchFileByVSWhere(host_cpu, target_cpu):
    program_files_x86 = os.environ['ProgramFiles(x86)']
    vswhere_path = os.path.join(
        program_files_x86, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if not os.path.exists(vswhere_path):
        return None, None
    cmd = '"%s" -latest -property installationPath' % vswhere_path
    vs_path = ExecuteCmd(cmd)
    if vs_path is None or vs_path == '':
        return None, None
    cmd = '"%s" -latest -property installationVersion' % vswhere_path
    vs_version_string = ExecuteCmd(cmd)
    if vs_version_string is None or vs_version_string == '':
        return None, None
    vs_version_part = list(map(lambda x: int(x), vs_version_string.split('.')))
    if len(vs_version_part) < 2:
        return None, None
    vs_version = vs_version_part[0] * 10 + (min(vs_version_part[1], 9))
    batch_file = os.path.join(
        vs_path, 'VC', 'Auxiliary', 'Build', 'vcvarsall.bat')
    if not os.path.exists(batch_file):
        return None, None
    if host_cpu == target_cpu:
        arch = target_cpu
    else:
        arch = host_cpu + '_' + target_cpu
    return vs_version, [batch_file, arch]


def DetectSetEnvBatchFileByEnvVar(host_cpu, target_cpu):
    regex = re.compile(r'VS(\d+)COMNTOOLS')
    vs_versions = []
    for vs in os.environ:
        m = regex.match(vs.upper())
        if m:
            vs_versions.append((int(m.group(1)), os.environ[vs]))
    vs_versions = sorted(vs_versions, key=lambda item: item[0], reverse=True)
    for version, path in vs_versions:
        if version > VS_2003_VERSION:
            batch_file = os.path.join(path, '..', '..', 'VC', 'vcvarsall.bat')
            if os.path.exists(batch_file):
                return version, '"' + batch_file + '" ' + target_cpu
        else:
            batch_file = os.path.join(path, 'vsvars32.bat')
            if os.path.exists(batch_file):
                return version, [batch_file]
    return None, None


def DetectSetEnvBatchFileByFindVC6(host_cpu, target_cpu):
    program_files_x86 = os.environ['ProgramFiles(x86)']
    batch_file = os.path.join(
        program_files_x86, 'Microsoft Visual Studio', 'VC98', 'Bin', 'vcvars32.bat')
    if not os.path.exists(batch_file):
        return None, None
    return VC_60_VERSION, [batch_file]


def FindWinSDK71(host_cpu, target_cpu):
    program_files_x86 = os.environ['ProgramFiles(x86)']
    sdk_dir = os.path.join(
        program_files_x86, 'Microsoft SDKs', 'Windows', 'v7.1A')
    include_path = os.path.join(sdk_dir, 'Include')
    lib_path = os.path.join(sdk_dir, 'Lib')
    if target_cpu == 'x64':
        lib_path = os.path.join(lib_path, 'x64')
    if not os.path.exists(include_path) or not os.path.exists(lib_path):
        return None, None
    return include_path, lib_path


def SetupEnvironment(host_cpu, target_cpu, is_winxp):
    version, cmd = DetectSetEnvBatchFileByVSWhere(host_cpu, target_cpu)
    if cmd is None:
        version, cmd = DetectSetEnvBatchFileByEnvVar(host_cpu, target_cpu)
    if cmd is None:
        version, cmd = DetectSetEnvBatchFileByFindVC6(host_cpu, target_cpu)
    if cmd is None:
        assert False, 'Cannot fine Windows SDK set-env batch file'
    if is_winxp:
        include_path, lib_path = FindWinSDK71(host_cpu, target_cpu)
        if include_path is None or lib_path is None:
            assert False, 'Cannot Windows SDK v7.1A, please make sure it is installed.'
    shell_cmd = '"%s" %s && Set' % (cmd[0], ' '.join(cmd[1:]))
    env_lines = ExecuteCmd(shell_cmd)
    ENV_VAR_TO_SAVE = (
        'INCLUDE',
        'LIB',
        'LIBPATH',
        'PATH',
        'PATHEXT',
        'SYSTEMROOT',
        'TEMP',
        'TMP',
    )
    env = {}
    for line in env_lines.split('\n'):
        kv = line.split('=', 2)
        if kv[0].upper() in ENV_VAR_TO_SAVE:
            env[kv[0].upper()] = kv[1].strip()
    if is_winxp:
        if version > VS_2017_VERSION:
            vc141_version_file = os.path.join(os.path.dirname(
                cmd[0]), 'Microsoft.VCToolsVersion.v141.default.txt')
            vc_version_file = os.path.join(os.path.dirname(
                cmd[0]), 'Microsoft.VCToolsVersion.default.txt')
            if not os.path.exists(vc141_version_file):
                assert False, 'v141_xp toolset not installed'
            with open(vc141_version_file, 'r') as f:
                vc141_version = f.read().strip()
            with open(vc_version_file, 'r') as f:
                vc_version = f.read().strip()
            env['INCLUDE'] = env['INCLUDE'].replace(vc_version, vc141_version)
            env['LIB'] = env['LIB'].replace(vc_version, vc141_version)
            env['LIBPATH'] = env['LIBPATH'].replace(vc_version, vc141_version)
            env['PATH'] = env['PATH'].replace(vc_version, vc141_version)

        env['INCLUDE'] = include_path + ';' + env['INCLUDE']
        env['LIB'] = lib_path + ';' + env['LIB']
        env['LIBPATH'] = lib_path + ';' + env['LIBPATH']

    env_file_content = ''
    for k in env:
        env_file_content += k + '=' + env[k] + '\0'
    env_file_content += '\0'
    with open('environment.%s.%s' % (host_cpu, target_cpu), 'w') as f:
        f.write(env_file_content)
    # print('VS_VERSION ="%s"' % vs_version_string)
    '''
    {
    'INCLUDE': 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\ATLMFC\\include;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\include;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.19041.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.19041.0\\shared;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.19041.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.19041.0\\winrt;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.19041.0\\cppwinrt',
    'LIB': 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\ATLMFC\\lib\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.19041.0\\ucrt\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.19041.0\\um\\x64',
    'LIBPATH': 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\ATLMFC\\lib\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\lib\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\lib\\x86\\store\\references;C:\\Program Files (x86)\\Windows Kits\\10\\UnionMetadata\\10.0.19041.0;C:\\Program Files (x86)\\Windows Kits\\10\\References\\10.0.19041.0;C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319',
    'PATH': 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\\\Extensions\\Microsoft\\IntelliCode\\CLI;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\bin\\HostX64\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\VC\\VCPackages;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\TestWindow;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\TeamFoundation\\Team Explorer;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\MSBuild\\Current\\bin\\Roslyn;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Team Tools\\Performance Tools\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Team Tools\\Performance Tools;C:\\Program Files (x86)\\Microsoft Visual Studio\\Shared\\Common\\VSPerfCollectionTools\\vs2019\\\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\Shared\\Common\\VSPerfCollectionTools\\vs2019\\;C:\\Program Files (x86)\\HTML Help Workshop;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\Tools\\devinit;C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.19041.0\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\bin\\x64;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\\\MSBuild\\Current\\Bin;C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\Tools\\;C:\\msys64\\usr\\bin;C:\\WINDOWS\\system32;C:\\WINDOWS;C:\\WINDOWS\\System32\\Wbem;C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\;C:\\WINDOWS\\System32\\OpenSSH\\;C:\\Users\\k34ub\\Downloads\\VSCode-win32-x64-1.84.2;C:\\Users\\k34ub\\Downloads\\cmake-3.28.0-windows-x86_64\\bin;C:\\Users\\k34ub\\.cargo\\bin;C:\\msys64\\clang64\\bin;C:\\msys64\\mingw64\\bin;C:\\Program Files\\Git\\bin;C:\\Users\\k34ub\\source\\skstu\\devtools\\bin\\gn\\gn-windows-amd64;C:\\Users\\k34ub\\source\\skstu\\devtools\\bin\\ninja\\ninja-win;C:\\Users\\k34ub\\.cargo\\bin;C:\\Users\\k34ub\\AppData\\Local\\Microsoft\\WindowsApps;;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\Llvm\\x64\\bin;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja;C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\VC\\Linux\\bin\\ConnectionManagerExe',
    'PATHEXT': '.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC',
    'SYSTEMROOT': 'C:\\WINDOWS',
    'TEMP': 'C:\\Users\\k34ub\\AppData\\Local\\Temp',
    'TMP': 'C:\\Users\\k34ub\\AppData\\Local\\Temp'
    }
    '''
    return version, env


def FindExecutableInPath(env_path, exe_name):
    paths = env_path.split(';')
    for path in paths:
        if os.path.exists(os.path.join(path, exe_name)):
            return path
    return None


def FindCompiles(env_path):
    cl_path = FindExecutableInPath(env_path, 'cl.exe')
    clang_path = FindExecutableInPath(env_path, 'clang.exe')
    return {'MSVC': cl_path, 'CLANG': clang_path}


def main():
    (host_cpu, target_cpu, is_winxp) = sys.argv[1:]
    version, env = SetupEnvironment(host_cpu, target_cpu, is_winxp == 'true')

    print('VERSION = %s' % version)
    cc_path = FindCompiles(env['PATH'])
    for cc in cc_path:
        print('%s = %s' % (cc, 'true' if cc_path[cc] is not None else 'false'))


if __name__ == '__main__':
    main()
