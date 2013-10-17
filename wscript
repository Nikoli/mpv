# vi: ft=python

import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'waftools'))
from waftools.checks import *
from waftools.custom_checks import *

main_dependencies = [
    {
        'name': 'ebx_available',
        'desc': 'ebx availability',
        'func': check_cc(fragment=load_fragment('ebx'))
    } , {
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
    }, {
        'name': '--libass',
        'desc': 'SSA/ASS support',
        'func': check_pkg_config('libass'),
        'req': True,
        'fmsg': "Unable to find development files for libass. Aborting. \
If you really mean to compile without libass support use --disable-libass."
    }, {
        'name': '--libass-osd',
        'desc': 'libass OSD support',
        'deps': [ 'libass' ],
        'func': check_true,
    }, {
        'name': '--dummy-osd',
        'desc': 'dummy OSD support',
        'deps_neg': [ 'libass-osd' ],
        'func': check_true,
    } , {
        'name': 'zlib',
        'desc': 'zlib',
        'func': check_libs(['z'],
                    check_statement('zlib.h', 'inflate(0, Z_NO_FLUSH)')),
        'req': True,
        'fmsg': 'Unable to find development files for zlib.'
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

    group = opt.add_option_group("Installation Directories")

    group.add_option("--datadir", type="string", default="share/mpv",
        help="directory for installing machine independent data files \
(skins, etc) [$PREFIX/share/mpv]")

    group.add_option("--mandir", type="string", default="share/man",
        help="directory for installing man pages [$PREFIX/share/man]")

    group.add_option("--confdir", type="string", default="etc/mpv",
        help="directory for installing configuration files [$PREFIX/etc/mpv]")

    group.add_option("--localedir", type="string", default="share/locale",
        help="directory for gettext locales [$PREFIX/share/locale]")

def configure(ctx):
    ctx.find_program('perl', var='BIN_PERL')
    ctx.find_program('cc', var='CC')
    ctx.load('compiler_c')
    ctx.load('waf_customizations')
    ctx.load('cpudetect')
    ctx.load('dependencies')
    ctx.detect_target_os_dependency()

    ctx.env.CFLAGS += ["-std=c99", "-Wall", "-Wno-switch", "-Wpointer-arith",
                       "-Wundef", "-Wno-pointer-sign", "-Wmissing-prototypes",
                       "-Werror=implicit-function-declaration",
                       # XXX: hax, please fix for gcc
                       "-fcolor-diagnostics"
                       ]

    ctx.parse_dependencies(main_dependencies)
    ctx.parse_dependencies(audio_output_features)
    ctx.parse_dependencies(video_output_features)
    ctx.parse_dependencies(libav_dependencies)

    if ctx.options.developer:
        print ctx.env


    ctx.define("MPLAYER_CONFDIR", ctx.options.confdir)
    ctx.define("MPLAYER_LOCALEDIR", ctx.options.localedir)

    from sys import argv
    ctx.define("CONFIGURATION", " ".join(argv))

    ctx.start_msg("Writing configuration header:")
    ctx.write_config_header('config.h')
    ctx.end_msg("config.h", "PINK")

    ctx.start_msg("Writing header:")
    import subprocess
    process = subprocess.Popen("./version.sh --print",
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               cwd=ctx.srcnode.abspath(), shell=True)
    process.wait()
    (version, err) = process.communicate()
    import time
    today = time.strftime("%a %b %d %H:%M:%S %Z %Y", time.gmtime())

    ctx.define("VERSION", version.strip())
    ctx.define("BUILDDATE", today)
    ctx.write_config_header("version.h")
    ctx.end_msg("version.h", "PINK")

def build(ctx):
    includes = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + \
               ctx.dependencies_includes()

    sources = [
        ( "audio/audio.c" ),
        ( "audio/chmap.c" ),
        ( "audio/chmap_sel.c" ),
        ( "audio/fmt-conversion.c" ),
        ( "audio/format.c" ),
        ( "audio/mixer.c" ),
        ( "audio/reorder_ch.c" ),
        ( "audio/decode/ad_lavc.c" ),
        ( "audio/decode/ad_mpg123.c",            "mpg123" ),
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
        ( "audio/out/ao_sndio.c",                "sndio" ),
        ( "audio/out/ao_wasapi.c",               "wasapi" ),
    ]

    ctx.objects(
        target   = "audio",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    sources = [
        ( "mpvcore/timeline/tl_cue.c" ),
        ( "mpvcore/timeline/tl_edl.c" ),
        ( "mpvcore/timeline/tl_matroska.c" ),
        ( "mpvcore/input/input.c" ),
        ( "mpvcore/input/joystick.c",            "joystick" ),
        ( "mpvcore/input/lirc.c",                "lirc"),
        ( "mpvcore/asxparser.c" ),
        ( "mpvcore/av_common.c" ),
        ( "mpvcore/av_log.c" ),
        ( "mpvcore/av_opts.c" ),
        ( "mpvcore/bstr.c" ),
        ( "mpvcore/charset_conv.c" ),
        ( "mpvcore/codecs.c" ),
        ( "mpvcore/command.c" ),
        ( "mpvcore/cpudetect.c" ),
        ( "mpvcore/encode_lavc.c",               "encoding" ),
        ( "mpvcore/m_config.c" ),
        ( "mpvcore/m_option.c" ),
        ( "mpvcore/m_property.c" ),
        ( "mpvcore/mp_common.c" ),
        ( "mpvcore/mp_lua.c",                    "lua" ),
        ( "mpvcore/mp_msg.c" ),
        ( "mpvcore/mp_ring.c" ),
        ( "mpvcore/mplayer.c" ),
        ( "mpvcore/options.c" ),
        ( "mpvcore/parser-cfg.c" ),
        ( "mpvcore/parser-mpcmd.c" ),
        ( "mpvcore/path.c" ),
        ( "mpvcore/playlist.c" ),
        ( "mpvcore/playlist_parser.c" ),
        ( "mpvcore/resolve_quvi.c",              "libquvi4"),
        ( "mpvcore/resolve_quvi9.c",             "libquvi9" ),
        ( "mpvcore/screenshot.c" ),
        ( "mpvcore/version.c" ),
    ]

    ctx.objects(
        target   = "core",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    ctx(
        rule    = "${BIN_PERL} %s/TOOLS/file2string.pl ${SRC} > ${TGT}" % ctx.srcnode.abspath(),
        source  = "etc/input.conf",
        target  = "core/input/input_conf.h",
        name    = "gen_input_conf",
        before  = ("c",)
    )

    ctx(
        rule    = "${BIN_PERL} %s/TOOLS/matroska.pl --generate-header ${SRC} > ${TGT}" % ctx.srcnode.abspath(),
        source  = "demux/ebml.c demux/demux_mkv.c",
        target  = "ebml_types.h",
        name    = "gen_ebml_types_h",
        before  = ("c",)
    )

    ctx(
        rule    = "${BIN_PERL} %s/TOOLS/matroska.pl --generate-definitions ${SRC} > ${TGT}" % ctx.srcnode.abspath(),
        source  = "demux/ebml.c",
        target  = "ebml_defs.c",
        name    = "gen_ebml_defs_c",
        before  = ("c",)
    )

    sources = [
        ( "demux/codec_tags.c" ),
        ( "demux/demux.c" ),
        ( "demux/demux_cue.c" ),
        ( "demux/demux_edl.c" ),
        ( "demux/demux_lavf.c" ),
        ( "demux/demux_libass.c",                "libass"),
        ( "demux/demux_mf.c" ),
        ( "demux/demux_mkv.c" ),
        ( "demux/demux_mng.c",                   "mng"),
        ( "demux/demux_playlist.c" ),
        ( "demux/demux_raw.c" ),
        ( "demux/demux_subreader.c" ),
        ( "demux/ebml.c" ),
        ( "demux/mf.c" ),
    ]

    ctx.objects(
        target   = "demux",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    sources = [
        ( "stream/ai_alsa1x.c",                  "alsa" ),
        ( "stream/ai_oss.c",                     "oss" ),
        ( "stream/ai_sndio.c",                   "sndio" ),
        ( "stream/audio_in.c",                   "audio_input" ),
        ( "stream/cache.c",                      "cache"),
        ( "stream/cdinfo.c",                     "cdda"),
        ( "stream/cookies.c" ),
        ( "stream/dvb_tune.c",                   "dvbin" ),
        ( "stream/frequencies.c",                "tv" ),
        ( "stream/rar.c" ),
        ( "stream/stream.c" ),
        ( "stream/stream_avdevice.c" ),
        ( "stream/stream_bluray.c",              "libbluray" ),
        ( "stream/stream_cdda.c",                "cdda" ),
        ( "stream/stream_dvb.c",                 "dvbin" ),
        ( "stream/stream_dvd.c",                 "dvdread" ),
        ( "stream/stream_dvd_common.c",          "dvdread" ),
        ( "stream/stream_file.c" ),
        ( "stream/stream_lavf.c" ),
        ( "stream/stream_memory.c" ),
        ( "stream/stream_mf.c" ),
        ( "stream/stream_null.c" ),
        ( "stream/stream_pvr.c",                 "pvr" ),
        ( "stream/stream_radio.c",               "radio" ),
        ( "stream/stream_rar.c" ),
        ( "stream/stream_smb.c",                 "libsmbclient" ),
        ( "stream/stream_tv.c",                  "tv" ),
        ( "stream/stream_vcd.c",                 "vcd" ),
        ( "stream/tv.c",                         "tv" ),
        ( "stream/tvi_dummy.c",                  "tv" ),
        ( "stream/tvi_v4l2.c",                   "tv_v4l2"),
    ]

    ctx.objects(
        target   = "stream",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    sources = [
        ( "sub/ass_mp.c",                        "libass"),
        ( "sub/dec_sub.c" ),
        ( "sub/draw_bmp.c" ),
        ( "sub/find_subfiles.c" ),
        ( "sub/img_convert.c" ),
        ( "sub/osd_dummy.c",                     "dummy-osd" ),
        ( "sub/osd_libass.c",                    "libass-osd" ),
        ( "sub/sd_ass.c",                        "libass" ),
        ( "sub/sd_lavc.c" ),
        ( "sub/sd_lavc_conv.c" ),
        ( "sub/sd_lavf_srt.c" ),
        ( "sub/sd_microdvd.c" ),
        ( "sub/sd_movtext.c" ),
        ( "sub/sd_spu.c" ),
        ( "sub/sd_srt.c" ),
        ( "sub/spudec.c" ),
        ( "sub/sub.c" ),
    ]

    ctx.objects(
        target   = "sub",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    sources = [
        ( "video/csputils.c" ),
        ( "video/fmt-conversion.c" ),
        ( "video/image_writer.c" ),
        ( "video/img_format.c" ),
        ( "video/mp_image.c" ),
        ( "video/mp_image_pool.c" ),
        ( "video/sws_utils.c" ),
        ( "video/vaapi.c",                       "vaapi" ),
        ( "video/vdpau.c",                       "vdpau" ),
        ( "video/decode/dec_video.c"),
        ( "video/decode/lavc_dr1.c",             "!avutil_refcounting" ),
        ( "video/decode/vaapi.c",                "vaapi" ),
        ( "video/decode/vd.c" ),
        ( "video/decode/vd_lavc.c" ),
        ( "video/decode/vda.c",                  "vda" ),
        ( "video/decode/vdpau.c",                "vdpau" ),
        ( "video/decode/vdpau_old.c",            "vdpau" ),
        ( "video/filter/pullup.c" ),
        ( "video/filter/vf.c" ),
        ( "video/filter/vf_crop.c" ),
        ( "video/filter/vf_delogo.c" ),
        ( "video/filter/vf_divtc.c" ),
        ( "video/filter/vf_dlopen.c",            "libdl" ),
        ( "video/filter/vf_down3dright.c" ),
        ( "video/filter/vf_dsize.c" ),
        ( "video/filter/vf_eq.c" ),
        ( "video/filter/vf_expand.c" ),
        ( "video/filter/vf_flip.c" ),
        ( "video/filter/vf_format.c" ),
        ( "video/filter/vf_gradfun.c" ),
        ( "video/filter/vf_hqdn3d.c" ),
        ( "video/filter/vf_ilpack.c" ),
        ( "video/filter/vf_lavfi.c" ),
        ( "video/filter/vf_mirror.c" ),
        ( "video/filter/vf_noformat.c" ),
        ( "video/filter/vf_noise.c" ),
        ( "video/filter/vf_phase.c" ),
        ( "video/filter/vf_pp.c",                "vfpp" ),
        ( "video/filter/vf_pullup.c" ),
        ( "video/filter/vf_rotate.c" ),
        ( "video/filter/vf_scale.c" ),
        ( "video/filter/vf_screenshot.c" ),
        ( "video/filter/vf_softpulldown.c" ),
        ( "video/filter/vf_stereo3d.c" ),
        ( "video/filter/vf_sub.c" ),
        ( "video/filter/vf_swapuv.c" ),
        ( "video/filter/vf_unsharp.c" ),
        ( "video/filter/vf_vavpp.c",             "vaapi-vpp"),
        ( "video/filter/vf_vo.c" ),
        ( "video/filter/vf_yadif.c" ),
        ( "video/out/aspect.c" ),
        ( "video/out/bitmap_packer.c" ),
        ( "video/out/dither.c" ),
        ( "video/out/filter_kernels.c" ),
        ( "video/out/gl_cocoa.c",                "gl_cocoa" ),
        ( "video/out/gl_common.c",               "opengl" ),
        ( "video/out/gl_lcms.c",                 "lcms" ),
        ( "video/out/gl_osd.c",                  "opengl" ),
        ( "video/out/gl_video.c",                "opengl" ),
        ( "video/out/gl_w32.c",                  "gl_w32" ),
        ( "video/out/gl_wayland.c",              "gl_wayland" ),
        ( "video/out/gl_x11.c",                  "gl_x11" ),
        ( "video/out/pnm_loader.c",              "opengl" ),
        ( "video/out/vo.c" ),
        ( "video/out/vo_caca.c",                 "caca" ),
        ( "video/out/vo_corevideo.c",            "corevideo"),
        ( "video/out/vo_direct3d.c",             "direct3d" ),
        ( "video/out/vo_image.c" ),
        ( "video/out/vo_lavc.c",                 "encoding" ),
        ( "video/out/vo_null.c" ),
        ( "video/out/vo_opengl.c",               "opengl" ),
        ( "video/out/vo_opengl_old.c",           "opengl" ),
        ( "video/out/vo_sdl.c",                  "sdl2" ),
        ( "video/out/vo_vaapi.c",                "vaapi" ),
        ( "video/out/vo_vdpau.c",                "vdpau" ),
        ( "video/out/vo_wayland.c",              "wayland" ),
        ( "video/out/vo_x11.c" ,                 "x11" ),
        ( "video/out/vo_xv.c",                   "xvideo" ),
        ( "video/out/w32_common.c",              "w32" ), ### XXX
        ( "video/out/wayland_common.c",          "wayland" ),
        ( "video/out/x11_common.c",              "x11" ),
    ]

    ctx.objects(
        target   = "video",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    getch2_c = {
        'win32':  'osdep/getch2-win.c',
    }.get(ctx.env.DEST_OS, "osdep/getch2.c")

    timer_c = {
        'win32':  'osdep/timer-win2.c',
        'darwin': 'osdep/timer-darwin.c',
    }.get(ctx.env.DEST_OS, "osdep/timer-linux.c")

    sources = [
        getch2_c,
        "osdep/io.c",
        "osdep/numcores.c",
        "osdep/timer.c",
        timer_c
    ]

    ctx.objects(
        target   = "osdep",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    sources = [ "ta/ta.c", "ta/ta_talloc.c", "ta/ta_utils.c", ]

    ctx.objects(
        target   = "tree_allocator",
        source   = ctx.filtered_sources(sources),
        includes = includes
    )

    ctx(
        target = "mpv",
        use    = [
            "audio",
            "core",
            "demux",
            "stream",
            "sub",
            "video",

            "osdep",
            "tree_allocator",
        ],
        lib      = ctx.dependencies_lib(),
        libpath  = ctx.dependencies_libpath(),
        features = "c cprogram"
    )
