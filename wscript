# vi: ft=python

import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'waftools'))
from waftools.checks import check_pkg_config, check_pkg_config_multiple

audio_output_features = {
    'portaudio': {
        'desc': 'PortAudio audio output',
        'deps': [ 'pthreads' ],
        'func': check_pkg_config('portaudio-2.0', '>= 19'),
    },
    'openal': {
        'desc': 'OpenAL audio output',
        'func': check_pkg_config('openal', '>= 1.13'),
        'default': 'disable'
    }
}

video_output_features = {
    'vdpau': {
        'desc': 'VDPAU acceleration',
        'deps': [ 'os_linux', 'x11' ],
        'func': check_pkg_config('vdpau', '>= 0.2'),
    },
    'vaapi': {
        'desc': 'VAAPI acceleration',
        # 'deps': [ 'os_linux', 'x11' ],
        'func': check_pkg_config_multiple(
            # 'vaapi', 'libva', '>= 0.32.0', 'libva-x11', '>= 0.32.0'),
            'libass', 'libass'),
    },
    'vda': {
        'desc': 'VDA acceleration',
        'deps': [ 'os_mac', 'cocoa' ],
        'func': check_pkg_config('asd', 'asd'),
    }
}

def options(opt):
    opt.load('compiler_c')
    opt.load('features')
    opt.parse_features('Audio Outputs', audio_output_features)
    opt.parse_features('Video Outputs', video_output_features)

def configure(ctx):
    ctx.load('compiler_c')
    ctx.load('dependencies')
    ctx.parse_dependencies(audio_output_features)
    ctx.parse_dependencies(video_output_features)

def build(ctx):
    pass
