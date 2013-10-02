# vi: ft=python

import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'waftools'))
from waftools.checks import check_pkg_config, check_cc, check_statement

main_dependencies = [
    {
        'name': '_lm',
        'desc': '-lm',
        'func': check_cc(lib='m')
    },
    {
        'name': 'nanosleep',
        'desc': 'nanosleep',
        'func': check_statement('time.h', 'nanosleep(0,0)')
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
        'deps': [ 'os_linux', 'x11', 'dl' ],
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
