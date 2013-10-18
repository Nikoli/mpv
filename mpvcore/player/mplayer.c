/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <ctype.h>

#ifdef PTW32_STATIC_LIB
#include <pthread.h>
#endif

#include <libavutil/intreadwrite.h>
#include <libavutil/attributes.h>
#include <libavutil/md5.h>
#include <libavutil/common.h>

#include <libavcodec/version.h>

#include "config.h"
#include "talloc.h"

#include "osdep/io.h"

#if defined(__MINGW32__) || defined(__CYGWIN__)
#include <windows.h>
#endif
#define WAKEUP_PERIOD 0.5
#include <string.h>
#include <unistd.h>

// #include <sys/mman.h>
#include <sys/types.h>
#ifndef __MINGW32__
#include <sys/ioctl.h>
#include <sys/wait.h>
#endif

#include <sys/time.h>
#include <sys/stat.h>

#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <limits.h>

#include <errno.h>

#include "mpvcore/mpv_global.h"
#include "mpvcore/mp_msg.h"
#include "av_log.h"


#include "mpvcore/m_option.h"
#include "mpvcore/m_config.h"
#include "mpvcore/resolve.h"
#include "mpvcore/m_property.h"

#include "sub/find_subfiles.h"
#include "sub/dec_sub.h"
#include "sub/sd.h"

#include "mpvcore/mp_osd.h"
#include "video/out/vo.h"
#include "mpvcore/screenshot.h"

#include "sub/sub.h"
#include "mpvcore/cpudetect.h"

#ifdef CONFIG_X11
#include "video/out/x11_common.h"
#endif

#ifdef CONFIG_COCOA
#include "osdep/macosx_application.h"
#endif

#include "audio/out/ao.h"

#include "mpvcore/codecs.h"

#include "osdep/getch2.h"
#include "osdep/timer.h"

#include "mpvcore/input/input.h"
#include "mpvcore/encode.h"

#include "osdep/priority.h"

#include "stream/tv.h"
#include "stream/stream_radio.h"
#ifdef CONFIG_DVBIN
#include "stream/dvbin.h"
#endif

//**************************************************************************//
//             Playtree
//**************************************************************************//
#include "mpvcore/playlist.h"
#include "mpvcore/playlist_parser.h"

//**************************************************************************//
//             Config
//**************************************************************************//
#include "mpvcore/parser-cfg.h"
#include "mpvcore/parser-mpcmd.h"

//**************************************************************************//
//             Config file
//**************************************************************************//

#include "mpvcore/path.h"

//**************************************************************************//
//**************************************************************************//
//             Input media streaming & demultiplexer:
//**************************************************************************//

#include "stream/stream.h"
#include "demux/demux.h"
#include "demux/stheader.h"

#include "audio/filter/af.h"
#include "audio/decode/dec_audio.h"
#include "video/decode/dec_video.h"
#include "video/mp_image.h"
#include "video/filter/vf.h"
#include "video/decode/vd.h"

#include "audio/mixer.h"

#include "mpvcore/mp_core.h"
#include "mpvcore/options.h"

#include "mp_lua.h"

const char mp_help_text[] = _(
"Usage:   mpv [options] [url|path/]filename\n"
"\n"
"Basic options:\n"
" --start=<time>    seek to given (percent, seconds, or hh:mm:ss) position\n"
" --no-audio        do not play sound\n"
" --no-video        do not play video\n"
" --fs              fullscreen playback\n"
" --sub=<file>      specify subtitle file to use\n"
" --playlist=<file> specify playlist file\n"
"\n"
" --list-options    list all mpv options\n"
"\n");

static const char av_desync_help_text[] = _(
"\n\n"
"           *************************************************\n"
"           **** Audio/Video desynchronisation detected! ****\n"
"           *************************************************\n\n"
"This means either the audio or the video is played too slowly.\n"
"Possible reasons, problems, workarounds:\n"
"- Your system is simply too slow for this file.\n"
"     Transcode it to a lower bitrate file with tools like HandBrake.\n"
"- Broken/buggy _audio_ driver.\n"
"     Experiment with different values for --autosync, 30 is a good start.\n"
"     If you have PulseAudio, try --ao=alsa .\n"
"- Slow video output.\n"
"     Try a different -vo driver (-vo help for a list) or try -framedrop!\n"
"- Playing a video file with --vo=opengl with higher FPS than the monitor.\n"
"     This is due to vsync limiting the framerate.\n"
"- Playing from a slow network source.\n"
"     Download the file instead.\n"
"- Try to find out whether audio or video is causing this by experimenting\n"
"  with --no-video and --no-audio.\n"
"If none of this helps you, file a bug report.\n\n");


//**************************************************************************//
//**************************************************************************//

#include "sub/ass_mp.h"


// ---

#include "mpvcore/mp_common.h"
#include "mpvcore/command.h"

static void reset_subtitles(struct MPContext *mpctx);
static void reinit_subs(struct MPContext *mpctx);
static void handle_force_window(struct MPContext *mpctx, bool reconfig);
