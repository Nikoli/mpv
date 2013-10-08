# vi: ft=python

import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'waftools'))
from waftools.checks import *
from waftools.custom_checks import *

main_dependencies = [
    {
        'name': 'libm',
        'desc': '-lm',
        'func': check_cc(lib='m')
    }, {
        'name': 'nanosleep',
        'desc': 'nanosleep',
        'func': check_statement('time.h', 'nanosleep(0,0)')
    }, {
        'name': 'libdl',
        'desc': 'dynamic loader',
        'func': check_libs(['dl'], check_statement('dlfcn.h', 'dlopen("", 0)'))
    }, {
        'name': 'pthreads',
        'desc': 'POSIX threads',
        'func': check_pthreads
    }, {
        'name': 'pthreads',
        'desc': 'POSIX threads (w32 static)',
        'deps': [ 'os_mingw32' ],
        'deps_neg': [ 'pthreads' ],
        'func': check_pthreads_w32_static
    }, {
        'name': 'librt',
        'desc': 'linking with -lrt',
        'deps': [ 'os_linux', 'pthreads' ],
        'func': check_cc(lib='rt')
    }, {
        'name': 'iconv',
        'desc': 'iconv',
        'func': check_iconv
    }, {
        'name': 'stream_cache',
        'desc': 'stream cache',
        'deps': [ 'pthreads' ],
        'func': check_true
    }, {
        'name': 'soundcard_h',
        'desc': 'soundcard.h',
        'deps': [ 'os_linux' ],
        'func': check_headers('soundcard.h', 'sys/soundcard.h')
    }, {
        'name': 'sys_videoio_h',
        'desc': 'videoio.h',
        'deps': [ 'os_linux' ],
        'func': check_headers('sys/videoio.h')
    }, {
        'name': 'terminfo',
        'desc': 'terminfo',
        'func': check_libs(['ncurses', 'ncursesw'],
            check_statement('term.h', 'setupterm(0, 1, 0)'))
    }, {
        'name': 'termcap',
        'desc': 'termcap',
        'deps_neg': ['terminfo'],
        'func': check_libs(['ncurses', 'tinfo', 'termcap'],
            check_statement('term.h', 'tgetent(0, 0)'))
    }, {
        'name': 'termios',
        'desc': 'termios',
        'func': check_headers('termios.h', 'sys/termios.h')
    }, {
        'name': 'shm',
        'desc': 'shm',
        'func': check_statement('sys/shm.h',
            'shmget(0, 0, 0); shmat(0, 0, 0); shmctl(0, 0, 0)')
    }, {
        'name': 'select',
        'desc': 'POSIX select()',
        'func': check_statement('sys/select.h', """
            int rc;
            rc = select(0, (fd_set *)(0), (fd_set *)(0), (fd_set *)(0),
                        (struct timeval *)(0))""")
    }, {
        # XXX : posix select / audio select
        'name': 'glob',
        'desc': 'glob()',
        'func': check_statement('glob.h', 'glob("filename", 0, 0, 0)')
    }, {
        'name': 'glob_win32_replacement',
        'desc': 'glob() win32 replacement',
        'deps_neg': [ 'glob' ],
        'deps': [ 'os_win32' ],
        'func': check_true
    }, {
        'name': 'setmode',
        'desc': 'setmode()',
        'func': check_statement('io.h', 'setmode(0, 0)')
    }, {
        'name': 'sys_sysinfo_h',
        'desc': 'sys/sysinfo.h',
        'func': check_statement('sys/sysinfo.h',
            'struct sysinfo s_info; s_info.mem_unit=0; sysinfo(&s_info)')
    }, {
        'name': 'libguess',
        'desc': 'libguess support',
        'func': check_pkg_config('libguess', '>= 1.0'),
    }, {
        'name': 'libsmbclient',
        'desc': 'Samba support',
        'deps': [ 'libdl' ],
        'func': check_libsmbclient,
        'module': 'input',
    }, {
        'name': 'libquvi4',
        'desc': 'libquvi 0.4.x support',
        'func': check_pkg_config('libquvi', '>= 0.4.1'),
    }, {
        'name': 'libquvi9',
        'desc': 'libquvi 0.9.x support',
        'deps_neg': [ 'libquvi4' ],
        'func': check_pkg_config('libquvi-0.9', '>= 0.9.0'),
    }
]

audio_output_features = [
    {
        'name': 'portaudio',
        'desc': 'PortAudio audio output',
        'deps': [ 'pthreads' ],
        'func': check_pkg_config('portaudio-2.0', '>= 19'),
    }, {
        'name': 'openal',
        'desc': 'OpenAL audio output',
        'func': check_pkg_config('openal', '>= 1.13'),
        'default': 'disable'
    }
]

video_output_features = [
    {
        'name': 'vdpau',
        'desc': 'VDPAU acceleration',
        'deps': [ 'os_linux', 'x11' ],
        'func': check_pkg_config('vdpau', '>= 0.2'),
    }, {
        'name': 'vaapi',
        'desc': 'VAAPI acceleration',
        'deps': [ 'os_linux', 'x11', 'libdl' ],
        'func': check_pkg_config(
            'libva', '>= 0.32.0', 'libva-x11', '>= 0.32.0'),
    }, {
        'name': 'vaapi-vpp',
        'desc': 'VAAPI VPP',
        'deps': [ 'vaapi' ],
        'func': check_pkg_config('libva', '>= 0.34.0'),
    }, {
        'name': 'vda',
        'desc': 'VDA acceleration',
        'deps': [ 'os_darwin', 'cocoa' ],
        'func': check_pkg_config('asd'),
    }
]

def options(opt):
    opt.load('compiler_c')
    opt.load('waf_customizations')
    opt.load('features')
    opt.parse_features('Audio Outputs', audio_output_features)
    opt.parse_features('Video Outputs', video_output_features)

    opt.add_option('--developer', action='store_true', default=False,
                   dest='developer', help='enable developer mode [disabled]')

def configure(ctx):
    ctx.load('compiler_c')
    ctx.load('waf_customizations')
    ctx.load('dependencies')
    ctx.detect_target_os_dependency()
    ctx.parse_dependencies(main_dependencies)
    ctx.parse_dependencies(audio_output_features)
    ctx.parse_dependencies(video_output_features)

    if ctx.options.developer:
        print ctx.env

    ctx.write_config_header('config.h')

def build(ctx):
    pass
