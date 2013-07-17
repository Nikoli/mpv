# vi: ft=python
# This file is not 80 wrapped yet please do not do that!

def options(ctx):
    ctx.load('compiler_c')

    Auto = None
    grp = ctx.add_option_group("General")
    grp.add_option("--disable-encoding",     action="store_false",   default=True,   help="disable encoding functionality [enable]")
    grp.add_option("--enable-termcap",       action="store_true",    default=Auto,   help="use termcap database for key codes [autodetect]")
    grp.add_option("--enable-termios",       action="store_true",    default=Auto,   help="use termios database for key codes [autodetect]")
    grp.add_option("--disable-iconv",        action="store_false",   default=Auto,   help="disable iconv for encoding conversion [autodetect]")
    grp.add_option("--enable-lirc",          action="store_true",    default=Auto,   help="enable LIRC (remote control) support [autodetect]")
    grp.add_option("--enable-lircc",         action="store_true",    default=Auto,   help="enable LIRCCD (LIRC client daemon) input [autodetect]")
    grp.add_option("--enable-joystick",      action="store_true",    default=False,  help="enable joystick support [disable]")
    grp.add_option("--disable-vm",           action="store_false",   default=Auto,   help="disable X video mode extensions [autodetect]")
    grp.add_option("--disable-xf86keysym",   action="store_false",   default=Auto,   help="disable support for multimedia keys [autodetect]")
    grp.add_option("--enable-radio",         action="store_true",    default=False,  help="enable radio interface [disable]")
    grp.add_option("--enable-radio-capture", action="store_true",    default=Auto,   help="enable radio capture (through PCI/line-in) [disable]")
    grp.add_option("--disable-radio-v4l2",   action="store_false",   default=Auto,   help="disable Video4Linux2 radio interface [autodetect]")
    grp.add_option("--disable-tv",           action="store_false",   default=True,   help="disable TV interface (TV/DVB grabbers) [enable]")
    grp.add_option("--disable-tv-v4l2",      action="store_false",   default=Auto,   help="disable Video4Linux2 TV interface [autodetect]")
    grp.add_option("--disable-pvr",          action="store_false",   default=Auto,   help="disable Video4Linux2 MPEG PVR [autodetect]")
    grp.add_option("--disable-networking",   action="store_false",   default=True,   help="disable networking [enable]")
    grp.add_option("--enable-winsock2_h",    action="store_true",    default=Auto,   help="enable winsock2_h [autodetect]")
    grp.add_option("--enable-smb",           action="store_true",    default=Auto,   help="enable Samba (SMB) input [autodetect]")
    grp.add_option("--enable-libquvi",       action="store_true",    default=Auto,   help="enable libquvi [autodetect]")
    grp.add_option("--enable-lcms2",         action="store_true",    default=Auto,   help="enable LCMS2 support [autodetect]")
    grp.add_option("--disable-vcd",          action="store_false",   default=Auto,   help="disable VCD support [autodetect]")
    grp.add_option("--disable-bluray",       action="store_false",   default=Auto,   help="disable Blu-ray support [autodetect]")
    grp.add_option("--disable-dvdread",      action="store_false",   default=Auto,   help="disable libdvdread [autodetect]")
    grp.add_option("--disable-cddb]",        action="store_false",   default=Auto,   help="disable cddb [autodetect]")
    grp.add_option("--disable-enca",         action="store_false",   default=Auto,   help="disable ENCA charset oracle library [autodetect]")
    grp.add_option("--enable-macosx-bundle", action="store_true",    default=Auto,   help="enable Mac OS X bundle file locations [autodetect]")
    grp.add_option("--disable-inet6",        action="store_false",   default=Auto,   help="disable IPv6 support [autodetect]")
    grp.add_option("--disable-gethostbyname2",action="store_false",  default=Auto,   help="gethostbyname2 part of the C library [autodetect]")
    grp.add_option("--disable-ftp",          action="store_false",   default=Auto,   help="disable FTP support [enabled]")
    grp.add_option("--disable-vstream",      action="store_false",   default=Auto,   help="disable TiVo vstream client support [autodetect]")
    grp.add_option("--disable-pthreads",     action="store_false",   default=Auto,   help="disable Posix threads support [autodetect]")
    grp.add_option("--disable-libass",       action="store_false",   default=Auto,   help="disable subtitle rendering with libass [autodetect]")
    grp.add_option("--disable-libass-osd",   action="store_false",   default=Auto,   help="disable OSD rendering with libass [autodetect]")
    grp.add_option("--enable-rpath",         action="store_true",    default=False,  help="enable runtime linker path for extra libs [disabled]")
    grp.add_option("--disable-libpostproc",  action="store_false",   default=Auto,   help="disable postprocess filter (vf_pp) [autodetect]")
    grp.add_option("--enable-libavdevice",   action="store_true",    default=False,  help="enable libavdevice demuxers [disabled]")
    grp.add_option("--enable-libavfilter",   action="store_true",    default=Auto,   help="enable libavfilter [disabled] [unused]")

    grp = ctx.add_option_group("Codecs")
    grp.add_option("--enable-mng",       action="store_true",    default=Auto,   help="enable MNG input support [autodetect]")
    grp.add_option("--enable-jpeg",      action="store_true",    default=Auto,   help="enable JPEG input/output support [autodetect]")
    grp.add_option("--enable-libcdio",   action="store_true",    default=Auto,   help="enable libcdio support [autodetect]")
    grp.add_option("--enable-libav",     action="store_true",    default=Auto,   help="skip Libav autodetection [autodetect]")
    grp.add_option("--disable-ladspa",   action="store_false",   default=Auto,   help="disable LADSPA plugin support [autodetect]")
    grp.add_option("--disable-libbs2b",  action="store_false",   default=Auto,   help="disable libbs2b audio filter support [autodetect]")
    grp.add_option("--disable-mpg123",   action="store_false",   default=Auto,   help="disable libmpg123 MP3 decoding support [autodetect]")

    grp = ctx.add_option_group("Resampler")
    grp.add_option("--disable-libavresample",  action="store_false",   default=Auto,   help="check for libswresample only [autodetect]")

    grp = ctx.add_option_group("Video")
    grp.add_option("--enable-gl",         action="store_true",   default=Auto,   help="enable OpenGL video output [autodetect]")
    grp.add_option("--enable-caca",       action="store_true",   default=Auto,   help="enable CACA  video output [autodetect]")
    grp.add_option("--enable-direct3d",   action="store_true",   default=Auto,   help="enable Direct3D video output [autodetect]")
    grp.add_option("--enable-sdl",        action="store_true",   default=Auto,   help="enable SDL audio output [autodetect]")
    grp.add_option("--enable-sdl2",       action="store_true",   default=Auto,   help="enable SDL 2.0+ audio and video output [autodetect]")
    grp.add_option("--enable-xv",         action="store_true",   default=Auto,   help="enable Xv video output [autodetect]")
    grp.add_option("--enable-vdpau",      action="store_true",   default=Auto,   help="enable VDPAU acceleration [autodetect]")
    grp.add_option("--enable-vm",         action="store_true",   default=Auto,   help="enable XF86VidMode support [autodetect]")
    grp.add_option("--enable-xinerama",   action="store_true",   default=Auto,   help="enable Xinerama support [autodetect]")
    grp.add_option("--enable-x11",        action="store_true",   default=Auto,   help="enable X11 video output [autodetect]")
    grp.add_option("--enable-wayland",    action="store_true",   default=Auto,   help="enable Wayland video output [autodetect]")
    grp.add_option("--disable-xss",       action="store_false",  default=Auto,   help="disable screensaver support via xss [autodetect]")
    grp.add_option("--disable-corevideo", action="store_false",  default=Auto,   help="disable CoreVideo video output [autodetect]")
    grp.add_option("--disable-cocoa",     action="store_false",  default=Auto,   help="disable Cocoa OpenGL backend [autodetect]")

    grp = ctx.add_option_group("Audio")
    grp.add_option("--disable-alsa",      action="store_false",  default=Auto,   help="disable ALSA audio output [autodetect]")
    grp.add_option("--disable-ossaudio",  action="store_false",  default=Auto,   help="disable OSS audio output [autodetect]")
    grp.add_option("--disable-rsound",    action="store_false",  default=Auto,   help="disable RSound audio output [autodetect]")
    grp.add_option("--disable-pulse",     action="store_false",  default=Auto,   help="disable Pulseaudio audio output [autodetect]")
    grp.add_option("--disable-portaudio", action="store_false",  default=Auto,   help="disable PortAudio audio output [autodetect]")
    grp.add_option("--disable-jack",      action="store_false",  default=Auto,   help="disable JACK audio output [autodetect]")
    grp.add_option("--enable-openal",     action="store_true",   default=False,  help="enable OpenAL audio output [disable]")
    grp.add_option("--disable-coreaudio", action="store_false",  default=Auto,   help="disable CoreAudio audio output [autodetect]")
    grp.add_option("--disable-dsound",    action="store_false",  default=Auto,   help="disable DirectSound audio output [autodetect]")
    grp.add_option("--disable-select",    action="store_false",  default=True,   help="disable using select() on the audio device [enable]")

    grp = ctx.add_option_group("Locale")
    grp.add_option("--enable-gettext",     action="store_true",  default=Auto,   help="enable gettext() usage [disable]")

    grp = ctx.add_option_group("Miscellanous")
    grp.add_option("--enable-cross-compile",  action="store_true",  default=False,         help="enable cross-compilation [disable]")
    grp.add_option("--pkg-config",       type="string",             default="pkg-config",  help="pkg-config to find some libraries [pkg-config]")
    grp.add_option("--windres",          type="string",             default="windres",     help="windres to build mpv [windres]")
    grp.add_option("--target",           type="string",             default='',            help="target platform (i386-linux, arm-linux, etc)")
    grp.add_option("--enable-static",    action="store_true",       default=Auto,          help="build a statically linked binary")
    grp.add_option("--with-install",     type="string",             default='',            help="path to a custom install program")
    grp.add_option("--disable-manpage",  action="store_false",      default=Auto,          help="do not build and install manpage [auto]")

    grp = ctx.add_option_group("Advanced")
    grp.add_option("--enable-shm",            action="store_true",   default=Auto,   help="enable shm [autodetect]")
    grp.add_option("--disable-debug",         action="store_false",  default=Auto,   help="compile-in debugging information [enable]")
    grp.add_option("--disable-optimization",  action="store_false",  default=Auto,   help="compile without -O2 [enable]")

    grp = ctx.add_option_group("Compiler")
    grp.add_option("--extra-cflags",   type="string", default='',   help="extra CFLAGS")
    grp.add_option("--extra-ldflags",  type="string", default='',   help="extra LDFLAGS")
    grp.add_option("--extra-libs",     type="string", default='',   help="extra linker flags")
    grp.add_option("--extra-libs-mpv", type="string", default='',   help="extra linker flags for mpv")

    grp = ctx.add_option_group("Installation Directories")
    grp.add_option("--datadir",   type="string", default="share/mpv",    help="directory for installing machine independent data files (skins, etc) [$PREFIX/share/mpv]")
    grp.add_option("--mandir",    type="string", default="share/man",    help="directory for installing man pages [$PREFIX/share/man]")
    grp.add_option("--confdir",   type="string", default="etc/mpv",      help="directory for installing configuration files [$PREFIX/etc/mpv]")
    grp.add_option("--localedir", type="string", default="share/locale", help="directory for gettext locales [$PREFIX/share/locale]")


def configure(ctx):
    ctx.load('compiler_c')

    from os import environ
    environ["PKG_CONFIG_PATH"] = "/mnt/devel/mpv/prefix/lib/pkgconfig/"

    ctx.env.DEFINES = ["_ISOC99_SOURCE", "_BSD_SOURCE"]
    ctx.env.CFLAGS = ["-std=c99", "-Wall", "-Wno-switch", "-Wpointer-arith", "-Wundef", "-Wno-pointer-sign", "-Wmissing-prototypes", "-Werror=implicit-function-declaration"]


    ctx.find_program("perl", var="BIN_PERL")

    # Headers required for building in (header, Whether it is manditory).
    header = [
        ("sys/socket.h", False),
        ("ws2tcpip.h",   False),
        ("sys/types.h",  True),
    ]

    for file, mandatory in header:
        ctx.check(header_name=file, features='c cprogram', mandatory=mandatory)

    # Check for socklen_t only if sys/socket.h exists.
    if "HAVE_SYS_SOCKET_H" in ctx.env.define_key:
        ctx.check_cc(type_name='socklen_t', header_name="sys/socket.h", mandatory=False)

    # Check for pkg-config libraries, format is ([package name => version], Whether it is manditory).
    pkg_config = {
        "vdpau":           (["vdpau >= 0.2"], False),
        "libquvi":         (["libquvi >= 0.4.1"], False),
        "caca":            (["caca >= 0.99.beta18"], False),
        "sdl2":            (["sdl2"], False),
        "sdl":             (["sdl"], False),
        "wayland-client":  (["wayland-client >= 1.0.0"], False),
        "wayland-egl":     (["wayland-egl >= 9.0.0"], False),
        "wayland-cursor":  (["wayland-cursor >= 1.0.0"], False),
        "xkbcommon":       (["xkbcommon >= 0.2.0"], False),
        "libpulse":        (["libpulse >= 0.9"], False),
        "portaudio":       (["portaudio-2.0 >= 19"], False),
        "jack":            (["jack"], False),
        "openal":          (["openal >= 1.13"], False),
        "alsa":            (["alsa >= 1.0.9"], False),
        "libbluray":       (["libbluray >= 0.2.1"], False),
        "dvdread":         (["dvdread >= 4.2.0"], False),
        "libcdio_paranoia":(["libcdio_paranoia"], False),
        "libass":          (["libass"], False),
        "libmpg123":       (["libmpg123 >= 1.2.0"], False),
        "libbs2b":         (["libbs2b"], False),
        "lcms2":           (["lcms2"], False),
        "libavresample":   (["libavresample >= 1.0.0"], False),
        "libswresample":   (["libswresample >= 0.15.100"], False),
        "libavfilter":     (["libavfilter >= 3.17.0"], False),
        "libavdevice":     (["libavdevice >= 54.0.0"], False),
        "libpostproc":     (["libpostproc >= 52.0.0"], False),
        "libavutil":       (["libavutil > 51.73.0"], False),
        "libavcodec":      (["libavcodec > 54.34.0"], False),
        "libavformat":     (["libavformat > 54.19.0"], False),
        "libswscale":      (["libswscale >= 2.0.0"], False),
    }

    for pkg in sorted(pkg_config):
        args, mandatory = pkg_config[pkg]
        ctx.check_cfg(package=pkg, args=args + ["--libs", "--cflags"], msg="Checking for %s" % " ".join(args), mandatory=mandatory)


    # Map internal mpv defines to waf internal, eventually these will be synched by replacing mpv source with waf-internal.
    config_map = {
        "HAVE_LIBAVRESAMPLE": "CONFIG_LIBAVRESAMPLE",
        "HAVE_LIBSWRESAMPLE": "CONFIG_LIBSWRESAMPLE",
    }

    for key in config_map:
        if key in ctx.env.define_key:
            ctx.define(config_map[key], key, quote=False)

    # String options supplied on the commandline that translate to strings in source.
    option_map = {
        "MPLAYER_CONFDIR": ctx.options.confdir,
        "MPLAYER_LOCALEDIR": ctx.options.localedir,
    }

    for key in option_map:
        ctx.define(key, option_map[key])

    # XXX: hack
    ctx.define("HAVE_DOS_PATHS", 0)

    # Configure flags used on commandline
    from sys import argv
    ctx.define("CONFIGURATION", " ".join(argv))


    # Write out configuration.
    ctx.start_msg("Writing configuration header:")
    ctx.write_config_header("config.h")
    ctx.end_msg("config.h", "PINK")


    # Create version.h
    ctx.start_msg("Writing header:")
    ctx.define("VERSION", "git-waf-whatever")
    ctx.define("BUILDDATE", "today")
    ctx.write_config_header("version.h")
    ctx.end_msg("version.h", "PINK")

    # XXX: hack
    # There is a proper way of doing this for now it's a hack until we collect all the required libraries.
    ctx.env.MPV_LIB = ctx.env.LIB_LIBAVFORMAT + ctx.env.LIB_LIBAVUTIL + ctx.env.LIB_LIBAVCODEC + ctx.env.LIB_LIBAVRESAMPLE + ctx.env.LIB_LIBSWSCALE
    ctx.env.MPV_LIBPATH = ctx.env.LIBPATH_LIBAVFORMAT + ctx.env.LIBPATH_LIBAVUTIL + ctx.env.LIBPATH_LIBAVCODEC + ctx.env.LIBPATH_LIBAVRESAMPLE + ctx.env.LIBPATH_LIBSWSCALE

    # Convenience functions
    from waflib.Logs import pprint
    def msg(str):
        pprint("YELLOW", str)

    def msg_setting(name, val):
        pprint("NORMAL", "  %-30s: " % name, sep="")
        pprint("YELLOW", val)

    # Print configuration to user
    msg("COMPILER SETTINGS")
    msg_setting("LIBS",    " ".join(ctx.env.MPV_LIB))
    msg_setting("LIBPATH", " ".join(ctx.env.MPV_LIBPATH))

def build(ctx):
    env = ctx.env

    source = [
        "audio/decode/ad.c",
        "audio/decode/ad_lavc.c",
        "audio/decode/ad_spdif.c",
        "audio/decode/dec_audio.c",
        "audio/filter/af.c",
        "audio/filter/af_center.c",
        "audio/filter/af_channels.c",
        "audio/filter/af_delay.c",
        "audio/filter/af_drc.c",
        "audio/filter/af_dummy.c",
        "audio/filter/af_equalizer.c",
        "audio/filter/af_extrastereo.c",
        "audio/filter/af_format.c",
        "audio/filter/af_hrtf.c",
        "audio/filter/af_karaoke.c",
        "audio/filter/af_lavcac3enc.c",
        "audio/filter/af_lavrresample.c",
        "audio/filter/af_pan.c",
        "audio/filter/af_scaletempo.c",
        "audio/filter/af_sinesuppress.c",
        "audio/filter/af_sub.c",
        "audio/filter/af_surround.c",
        "audio/filter/af_sweep.c",
        "audio/filter/af_tools.c",
        "audio/filter/af_volume.c",
        "audio/filter/filter.c",
        "audio/filter/window.c",
        "audio/format.c",
        "audio/mixer.c",
        "audio/out/ao.c",
        "audio/out/ao_null.c",
        "audio/out/ao_pcm.c",
        "audio/reorder_ch.c",
        "audio/chmap.c",
        "audio/audio.c",
        "audio/fmt-conversion.c",
        "audio/filter/af_force.c",
        "audio/chmap_sel.c",
    ]

    ctx.objects(
        target      = "audio",
        source      = source,
        includes    = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
    )

    source = [
        "core/asxparser.c",
        "core/av_common.c",
        "core/av_log.c",
        "core/av_opts.c",
        "core/bstr.c",
        "core/codecs.c",
        "core/command.c",
        "core/cpudetect.c",
        #- "core/defaultopts.c",
        "core/input/input.c",
        "core/mplayer.c",
        "core/mp_common.c",
        #- "core/mp_fifo.c",
        "core/mp_msg.c",
        "core/m_config.c",
        "core/m_option.c",
        "core/m_property.c",
        "core/m_struct.c",
        "core/parser-cfg.c",
        "core/parser-mpcmd.c",
        "core/path.c",
        "core/playlist.c",
        "core/playlist_parser.c",
        "core/screenshot.c",
        "core/subopt-helper.c",
        "core/timeline/tl_cue.c",
        "core/timeline/tl_edl.c",
        "core/timeline/tl_matroska.c",
        "core/version.c",
        "core/options.c",
        "core/charset_conv.c"
    ]

    ctx.objects(
        target      = "core",
        source      = source,
        includes    = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
        use         = "gen_input_conf"
    )

    ctx(
        rule    = "${BIN_PERL} %s/TOOLS/file2string.pl ${SRC} > ${TGT}" % ctx.srcnode.abspath(),
        source  = "etc/input.conf",
        target  = "input_conf.h",
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

    source = [
        #- "demux/asfheader.c",
        #- "demux/aviheader.c",
        #- "demux/aviprint.c",
        "demux/codec_tags.c",
        "demux/demux.c",
        #- "demux/demux_asf.c",
        #- "demux/demux_avi.c",
        "demux/demux_cue.c",
        "demux/demux_edl.c",
        "demux/demux_lavf.c",
        "demux/demux_mf.c",
        "demux/demux_mkv.c",
        #- "demux/demux_mng.c",
        #- "demux/demux_rawaudio.c",
        #- "demux/demux_rawvideo.c",
        #- "demux/demux_ts.c",
        "demux/ebml.c",
        #- "demux/extension.c",
        "demux/mf.c",
        #- "demux/mp3_hdr.c",
        #- "demux/mpeg_hdr.c",
        #- "demux/parse_es.c",
        #- "demux/video.c"
        "demux/demux_subreader.c",
        #- "demux/demux_libass.c",
        "demux/demux_raw.c"
    ]

    ctx.objects(
        target   = "demux",
        source   = source,
        includes = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
    )

    source = [
        "stream/stream.c",
        "stream/stream_avdevice.c",
        "stream/stream_file.c",
        "stream/stream_lavf.c",
        "stream/stream_mf.c",
        "stream/stream_null.c",
        "stream/stream_memory.c",
        "stream/cookies.c",
        #- "stream/url.c",
    ]

    ctx.objects(
        target    = "stream",
        source    = source,
        includes  = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
    )


    source = [
        "sub/dec_sub.c",
        "sub/draw_bmp.c",
        #- "sub/find_sub.c",
        "sub/find_subfiles.c",
        "sub/img_convert.c",
        "sub/sd_lavc.c",
        "sub/spudec.c",
        "sub/sub.c",
        #- "sub/subassconvert.c",
        #- "sub/subreader.c",
        "sub/sd_microdvd.c",
        "sub/sd_movtext.c",
        "sub/sd_lavf_srt.c",
        "sub/sd_lavc_conv.c",
        "sub/sd_spu.c",
        "sub/sd_srt.c"
    ]

    ctx.objects(
        target   = "sub",
        source   = source,
        includes = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
    )

    source = [
        "osdep/io.c",
        "osdep/numcores.c",
        "talloc.c",
        "osdep/timer-linux.c",
        "osdep/getch2.c",
        "video/decode/lavc_dr1.c", # refcounting code.
        "sub/osd_dummy.c", # XXX: insert only if osd is disabled.
        "osdep/timer.c"
    ]

    ctx.objects(
        target   = "misc",
        source   = source,
        includes = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
    )

    source = [
        "video/csputils.c",
        "video/decode/dec_video.c",
        "video/decode/vd.c",
        "video/decode/vd_lavc.c",
        "video/filter/pullup.c",
        "video/filter/vf.c",
        "video/filter/vf_crop.c",
        "video/filter/vf_delogo.c",
        "video/filter/vf_divtc.c",
        "video/filter/vf_dlopen.c",
        "video/filter/vf_down3dright.c",
        "video/filter/vf_dsize.c",
        "video/filter/vf_eq.c",
        "video/filter/vf_expand.c",
        "video/filter/vf_flip.c",
        "video/filter/vf_format.c",
        "video/filter/vf_gradfun.c",
        "video/filter/vf_hqdn3d.c",
        "video/filter/vf_ilpack.c",
        "video/filter/vf_mirror.c",
        "video/filter/vf_noformat.c",
        "video/filter/vf_noise.c",
        "video/filter/vf_phase.c",
        "video/filter/vf_pullup.c",
        "video/filter/vf_rotate.c",
        "video/filter/vf_scale.c",
        "video/filter/vf_screenshot.c",
        "video/filter/vf_softpulldown.c",
        "video/filter/vf_stereo3d.c",
        "video/filter/vf_sub.c",
        "video/filter/vf_swapuv.c",
        "video/filter/vf_unsharp.c",
        "video/filter/vf_vo.c",
        "video/filter/vf_yadif.c",
        "video/fmt-conversion.c",
        "video/image_writer.c",
        "video/img_format.c",
        "video/mp_image.c",
        "video/mp_image_pool.c",
        "video/out/aspect.c",
        "video/out/bitmap_packer.c",
        "video/out/filter_kernels.c",
        "video/out/vo.c",
        "video/out/vo_image.c",
        "video/out/vo_null.c",
        "video/sws_utils.c"
    ]

    ctx.objects(
        target   = "video",
        source   = source,
        includes = [ctx.bldnode.abspath(), ctx.srcnode.abspath()] + env.INCLUDES_LIBAVUTIL,
    )

    ctx(
        target = "mpv",
        use    = [
            "audio",
            "core",
            "demux",
            "misc",
            "stream",
            "sub",
            "video"
        ],
        rpath    = "/mnt/devel/mpv/prefix/lib", #XXX: hack to preserve sanity.
        lib      = ctx.env.MPV_LIB,
        libpath  = ctx.env.MPV_LIBPATH,
        features = "c cprogram"
    )
