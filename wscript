# vi: ft=python

import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'waftools'))
from waftools.checks import *

main_dependencies = [
    {
        'name': '_lm',
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
  # XXX: pthreads, lrt, iconv, rpath
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
        # XXX : posix select / audio select
        'name': 'glob',
        'desc': 'glob()',
        'func': check_statement('glob.h', 'glob("filename", 0, 0, 0)')
    }, {
        'name': 'glob_replacement',
        'desc': 'glob() win32 replacement',
        'deps_neg': [ 'glob' ],
        'deps': [ 'os_win32' ],
        'func': check_true
    }
]

audio_output_features = [
    {
        'name': 'portaudio',
        'desc': 'PortAudio audio output',
        'deps': [ 'pthreads' ],
        'func': check_pkg_config('portaudio-2.0', '>= 19'),
    },
    {
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
    },
    {
        'name': 'vaapi',
        'desc': 'VAAPI acceleration',
        'deps': [ 'os_linux', 'x11', 'libdl' ],
        'func': check_pkg_config(
            'libva', '>= 0.32.0', 'libva-x11', '>= 0.32.0'),
    },
    {
        'name': 'vaapi-vpp',
        'desc': 'VAAPI VPP',
        'deps': [ 'vaapi' ],
        'func': check_pkg_config('libva', '>= 0.34.0'),
    },
    {
        'name': 'vda',
        'desc': 'VDA acceleration',
        'deps': [ 'os_mac', 'cocoa' ],
        'func': check_pkg_config('asd'),
    }
]

def options(opt):
    opt.load('compiler_c')
    opt.load('features')
    opt.parse_features('Audio Outputs', audio_output_features)
    opt.parse_features('Video Outputs', video_output_features)

def configure(ctx):
    ctx.load('compiler_c')
    ctx.load('dependencies')
    ctx.parse_dependencies(main_dependencies)
    ctx.parse_dependencies(audio_output_features)
    ctx.parse_dependencies(video_output_features)

def build(ctx):
    pass
