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
        'name': '--pthreads',
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
        'name': '--iconv',
        'desc': 'iconv',
        'func': check_iconv,
        'req': True,
        'fmsg': "Unable to find iconv which should be part of a standard \
compilation environment. Aborting. If you really mean to compile without \
iconv support use --disable-iconv.",
    }, {
        'name': 'dos_paths',
        'desc': 'w32/dos paths',
        'deps': [ 'os_win32' ],
        'func': check_true
    }, {
        'name': 'priority',
        'desc': 'w32 priority API',
        'deps': [ 'os_win32' ],
        'func': check_true
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
        'name': '--terminfo',
        'desc': 'terminfo',
        'func': check_libs(['ncurses', 'ncursesw'],
            check_statement('term.h', 'setupterm(0, 1, 0)')),
    }, {
        'name': '--termcap',
        'desc': 'termcap',
        'deps_neg': ['terminfo'],
        'func': check_libs(['ncurses', 'tinfo', 'termcap'],
            check_statement('term.h', 'tgetent(0, 0)')),
    }, {
        'name': '--termios',
        'desc': 'termios',
        'func': check_headers('termios.h', 'sys/termios.h'),
    }, {
        'name': '--shm',
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
        'name': '--libguess',
        'desc': 'libguess support',
        'func': check_pkg_config('libguess', '>= 1.0'),
    }, {
        'name': '--libsmbclient',
        'desc': 'Samba support',
        'deps': [ 'libdl' ],
        'func': check_libsmbclient,
        'module': 'input',
    }, {
        'name': '--libquvi4',
        'desc': 'libquvi 0.4.x support',
        'func': check_pkg_config('libquvi', '>= 0.4.1'),
    }, {
        'name': '--libquvi9',
        'desc': 'libquvi 0.9.x support',
        'deps_neg': [ 'libquvi4' ],
        'func': check_pkg_config('libquvi-0.9', '>= 0.9.0'),
    }
]

libav_pkg_config_checks = [
    'libavutil',   '> 51.73.0',
    'libavcodec',  '> 54.34.0',
    'libavformat', '> 54.19.0',
    'libswscale',  '>= 2.0.0'
]

libav_dependencies = [
    {
        'name': 'libav',
        'desc': 'libav/ffmpeg',
        'func': check_pkg_config(*libav_pkg_config_checks),
        'req': True,
        'fmsg': "Unable to find development files for some of the required \
Libav libraries ({0}). Aborting.".format(" ".join(libav_pkg_config_checks))
    }, {
        'name': 'libavresample',
        'desc': 'libavresample',
        'func': check_pkg_config('libavresample',  '>= 1.0.0'),
    }, {
        'name': 'avresample_set_channel_mapping',
        'desc': 'libavresample channel mapping API',
        'deps': [ 'libavresample' ],
        'func': check_statement('libavresample/avresample.h',
                                'avresample_set_channel_mapping(NULL, NULL)',
                                use='libavresample'),
    }, {
        'name': 'libswresample',
        'desc': 'libswresample',
        'func': check_pkg_config('libswresample', '>= 0.17.102'),
    }, {
        'name': 'resampler',
        'desc': 'usable resampler found',
        'deps': [ 'libavresample', 'libswresample' ],
        'func': check_true,
        'req':  True,
        'fmsg': 'No resampler found. Install libavresample or libswresample (FFmpeg).'
    }, {
        'name': 'av_codec_new_vdpau_api',
        'desc': 'libavcodec new vdpau API',
        'func': check_statement('libavutil/pixfmt.h',
                                'int x = AV_PIX_FMT_VDPAU'),
    }, {
        'name': 'avcodec_chroma_pos_api',
        'desc': 'libavcodec avcodec_enum_to_chroma_pos API',
        'func': check_statement('libavcodec/avcodec.h', """int x, y;
            avcodec_enum_to_chroma_pos(&x, &y, AVCHROMA_LOC_UNSPECIFIED)""",
            use='libav')
    }, {
        'name': 'avutil_qp_api',
        'desc': 'libavutil QP API',
        'func': check_statement('libavutil/frame.h',
                                'av_frame_get_qp_table(NULL, NULL, NULL)',
                                use='libav')
    }, {
        'name': 'avutil_refcounting',
        'desc': 'libavutil ref-counting API',
        'func': check_statement('libavutil/frame.h', 'av_frame_unref(NULL)',
                                use='libav'),
    } , {
        'name': 'av_opt_set_int_list',
        'desc': 'libavutil av_opt_set_int_list() API',
        'func': check_statement('libavutil/opt.h',
                                'av_opt_set_int_list(0,0,(int*)0,0,0)',
                                use='libav')
    }, {
        'name': '--libavfilter',
        'desc': 'libavfilter',
        'func': compose_checks(
            check_pkg_config('libavfilter'),
            check_cc(fragment=load_fragment('libavfilter'),
                     use='libavfilter')),
    }, {
        'name': '--vf-lavfi',
        'desc': 'using libavfilter through vf_lavfi',
        'deps': [ 'libavfilter', 'avutil_refcounting' ],
        'func': check_true
    }, {
        'name': '--af-lavfi',
        'desc': 'using libavfilter through af_lavfi',
        'deps': [ 'libavfilter', 'av_opt_set_int_list' ],
        'func': check_true
    }, {
        'name': '--libavdevice',
        'desc': 'libavdevice',
        'func': check_pkg_config('libavdevice', '>= 54.0.0'),
    }, {
        'name': '--libpostproc',
        'desc': 'libpostproc',
        'func': check_pkg_config('libpostproc', '>= 52.0.0'),
    }
]

audio_output_features = [
    {
        'name': 'audio_select',
        'desc': 'audio select()',
        'deps': [ 'select' ],
        'func': check_true,
    }, {
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

    optional_features = main_dependencies + libav_dependencies
    opt.filter_and_parse_features('Optional Feaures', optional_features)
    opt.parse_features('Audio Outputs', audio_output_features)
    opt.parse_features('Video Outputs', video_output_features)

    opt.add_option('--developer', action='store_true', default=False,
                   dest='developer', help='enable developer mode [disabled]')

def configure(ctx):
    ctx.find_program('cc', var='CC')
    ctx.load('compiler_c')
    ctx.load('waf_customizations')
    ctx.load('dependencies')
    ctx.detect_target_os_dependency()


    ctx.env.CFLAGS += ["-std=c99", "-Wall", "-Wno-switch", "-Wpointer-arith",
                       "-Wundef", "-Wno-pointer-sign", "-Wmissing-prototypes",
                       "-Werror=implicit-function-declaration",
                       # XXX: hax, please fix for gcc
                       # "-fcolor-diagnostics"
                       ]

    ctx.parse_dependencies(main_dependencies)
    ctx.parse_dependencies(audio_output_features)
    ctx.parse_dependencies(video_output_features)
    ctx.parse_dependencies(libav_dependencies)

    if ctx.options.developer:
        print ctx.env

    ctx.write_config_header('config.h')

def build(ctx):
    sources = [
        ( "audio/audio.c" ),
        ( "audio/chmap.c" ),
        ( "audio/chmap_sel.c" ),
        ( "audio/fmt-conversion.c" ),
        ( "audio/format.c" ),
        ( "audio/mixer.c" ),
        ( "audio/reorder_ch.c" ),
        ( "audio/decode/ad_lavc.c" ),
        ( "audio/decode/ad_mpg123.c" ),
        ( "audio/decode/ad_spdif.c" ),
        ( "audio/decode/dec_audio.c" ),
        ( "audio/filter/af.c" ),
        ( "audio/filter/af_bs2b.c",              "bs2b" ),
        ( "audio/filter/af_center.c" ),
        ( "audio/filter/af_channels.c" ),
        ( "audio/filter/af_delay.c" ),
        ( "audio/filter/af_drc.c" ),
        ( "audio/filter/af_dummy.c" ),
        ( "audio/filter/af_equalizer.c" ),
        ( "audio/filter/af_export.c" ),
        ( "audio/filter/af_extrastereo.c" ),
        ( "audio/filter/af_force.c" ),
        ( "audio/filter/af_format.c" ),
        ( "audio/filter/af_hrtf.c" ),
        ( "audio/filter/af_karaoke.c" ),
        ( "audio/filter/af_ladspa.c",            "ladspa" ),
        ( "audio/filter/af_lavcac3enc.c" ),
        ( "audio/filter/af_lavfi.c" ),
        ( "audio/filter/af_lavrresample.c" ),
        ( "audio/filter/af_pan.c" ),
        ( "audio/filter/af_scaletempo.c" ),
        ( "audio/filter/af_sinesuppress.c" ),
        ( "audio/filter/af_sub.c" ),
        ( "audio/filter/af_surround.c" ),
        ( "audio/filter/af_sweep.c" ),
        ( "audio/filter/af_tools.c" ),
        ( "audio/filter/af_volume.c" ),
        ( "audio/filter/filter.c" ),
        ( "audio/filter/window.c" ),
        ( "audio/out/ao.c" ),
        ( "audio/out/ao_alsa.c",                 "alsa" ),
        ( "audio/out/ao_coreaudio.c",            "coreaudio" ),
        ( "audio/out/ao_coreaudio_properties.c", "coreaudio" ),
        ( "audio/out/ao_coreaudio_utils.c",      "coreaudio" ),
        ( "audio/out/ao_dsound.c",               "dsound" ),
        ( "audio/out/ao_jack.c",                 "jack" ),
        ( "audio/out/ao_lavc.c",                 "encoding" ),
        ( "audio/out/ao_null.c" ),
        ( "audio/out/ao_openal.c",               "openal" ),
        ( "audio/out/ao_oss.c",                  "oss" ),
        ( "audio/out/ao_pcm.c" ),
        ( "audio/out/ao_portaudio.c",            "portaudio" ),
        ( "audio/out/ao_pulse.c",                "pulseaudio" ),
        ( "audio/out/ao_rsound.c",               "rsound" ),
        ( "audio/out/ao_sdl.c",                  "sdl" ),
        ( "audio/out/ao_wasapi.c",               "wasapi" ),
    ]

    ctx.objects(
        target   = "audio",
        source   = ctx.filtered_sources(sources),
        includes = [ctx.bldnode.abspath(), ctx.srcnode.abspath()],
    )

    ctx(
        target = "mpv",
        use    = [
            "audio",
            # "core",
            # "demux",
            # "misc",
            # "stream",
            # "sub",
            # "video"
        ],
        # lib      = ctx.env.MPV_LIB,
        # libpath  = ctx.env.MPV_LIBPATH,
        features = "c cprogram"
    )
