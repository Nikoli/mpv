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

#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include <libavutil/avstring.h>
#include <libavutil/common.h>

#include "config.h"
#include "talloc.h"
#include "command.h"
#include "input/input.h"
#include "stream/stream.h"
#include "demux/demux.h"
#include "demux/stheader.h"
#include "resolve.h"
#include "playlist.h"
#include "playlist_parser.h"
#include "sub/sub.h"
#include "sub/dec_sub.h"
#include "mpvcore/m_option.h"
#include "m_property.h"
#include "m_config.h"
#include "video/filter/vf.h"
#include "video/decode/vd.h"
#include "mp_osd.h"
#include "video/out/vo.h"
#include "video/csputils.h"
#include "playlist.h"
#include "audio/mixer.h"
#include "audio/out/ao.h"
#include "mpvcore/mp_common.h"
#include "audio/filter/af.h"
#include "video/decode/dec_video.h"
#include "audio/decode/dec_audio.h"
#include "mpvcore/path.h"
#include "stream/tv.h"
#include "stream/stream_radio.h"
#include "stream/pvr.h"
#ifdef CONFIG_DVBIN
#include "stream/dvbin.h"
#endif
#ifdef CONFIG_DVDREAD
#include "stream/stream_dvd.h"
#endif
#include "screenshot.h"
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include "mpvcore/mp_core.h"

#include "mp_lua.h"

struct command_ctx {
    int events;

#define OVERLAY_MAX_ID 64
    void *overlay_map[OVERLAY_MAX_ID];
};

static int edit_filters(struct MPContext *mpctx, enum stream_type mediatype,
                        const char *cmd, const char *arg);
static int set_filters(struct MPContext *mpctx, enum stream_type mediatype,
                       struct m_obj_settings *new_chain);

static bool reinit_filters(MPContext *mpctx, enum stream_type mediatype)
{
    switch (mediatype) {
    case STREAM_VIDEO:
        return reinit_video_filters(mpctx) >= 0;
    case STREAM_AUDIO:
        return reinit_audio_filters(mpctx) >= 0;
    }
    return false;
}

static const char *filter_opt[STREAM_TYPE_COUNT] = {
    [STREAM_VIDEO] = "vf",
    [STREAM_AUDIO] = "af",
};

static int set_filters(struct MPContext *mpctx, enum stream_type mediatype,
                       struct m_obj_settings *new_chain)
{
    bstr option = bstr0(filter_opt[mediatype]);
    struct m_config_option *co = m_config_get_co(mpctx->mconfig, option);
    if (!co)
        return -1;

    struct m_obj_settings **list = co->data;
    struct m_obj_settings *old_settings = *list;
    *list = NULL;
    m_option_copy(co->opt, list, &new_chain);

    bool success = reinit_filters(mpctx, mediatype);

    if (success) {
        m_option_free(co->opt, &old_settings);
    } else {
        m_option_free(co->opt, list);
        *list = old_settings;
        reinit_filters(mpctx, mediatype);
    }

    if (mediatype == STREAM_VIDEO)
        mp_force_video_refresh(mpctx);

    return success ? 0 : -1;
}

static int edit_filters(struct MPContext *mpctx, enum stream_type mediatype,
                        const char *cmd, const char *arg)
{
    bstr option = bstr0(filter_opt[mediatype]);
    struct m_config_option *co = m_config_get_co(mpctx->mconfig, option);
    if (!co)
        return -1;

    // The option parser is used to modify the filter list itself.
    char optname[20];
    snprintf(optname, sizeof(optname), "%.*s-%s", BSTR_P(option), cmd);

    struct m_obj_settings *new_chain = NULL;
    m_option_copy(co->opt, &new_chain, co->data);

    int r = m_option_parse(co->opt, bstr0(optname), bstr0(arg), &new_chain);
    if (r >= 0)
        r = set_filters(mpctx, mediatype, new_chain);

    m_option_free(co->opt, &new_chain);

    return r >= 0 ? 0 : -1;
}

static int edit_filters_osd(struct MPContext *mpctx, enum stream_type mediatype,
                            const char *cmd, const char *arg, bool on_osd)
{
    int r = edit_filters(mpctx, mediatype, cmd, arg);
    if (on_osd) {
        if (r >= 0) {
            const char *prop = filter_opt[mediatype];
            show_property_osd(mpctx, prop, MP_ON_OSD_MSG);
        } else {
            set_osd_tmsg(mpctx, OSD_MSG_TEXT, 1, mpctx->opts->osd_duration,
                         "Changing filters failed!");
        }
    }
    return r;
}

#ifdef HAVE_SYS_MMAN_H

static int ext2_sub_find(struct MPContext *mpctx, int id)
{
    struct command_ctx *cmd = mpctx->command_ctx;
    struct sub_bitmaps *sub = &mpctx->osd->external2;
    void *p = NULL;
    if (id >= 0 && id < OVERLAY_MAX_ID)
        p = cmd->overlay_map[id];
    if (sub && p) {
        for (int n = 0; n < sub->num_parts; n++) {
            if (sub->parts[n].bitmap == p)
                return n;
        }
    }
    return -1;
}

static int ext2_sub_alloc(struct MPContext *mpctx)
{
    struct osd_state *osd = mpctx->osd;
    struct sub_bitmaps *sub = &osd->external2;
    struct sub_bitmap b = {0};
    MP_TARRAY_APPEND(osd, sub->parts, sub->num_parts, b);
    return sub->num_parts - 1;
}

static int overlay_add(struct MPContext *mpctx, int id, int x, int y,
                       char *file, int offset, char *fmt, int w, int h,
                       int stride)
{
    struct command_ctx *cmd = mpctx->command_ctx;
    struct osd_state *osd = mpctx->osd;
    if (strcmp(fmt, "bgra") != 0) {
        MP_ERR(mpctx, "overlay_add: unsupported OSD format '%s'\n", fmt);
        return -1;
    }
    if (id < 0 || id >= OVERLAY_MAX_ID) {
        MP_ERR(mpctx, "overlay_add: invalid id %d\n", id);
        return -1;
    }
    int fd = -1;
    bool close_fd = true;
    if (file[0] == '@') {
        char *end;
        fd = strtol(&file[1], &end, 10);
        if (!file[1] || end[0])
            fd = -1;
        close_fd = false;
    } else {
        fd = open(file, O_RDONLY | O_BINARY);
    }
    void *p = mmap(NULL, h * stride, PROT_READ, MAP_SHARED, fd, offset);
    if (fd >= 0 && close_fd)
        close(fd);
    if (!p) {
        MP_ERR(mpctx, "overlay_add: could not open or map '%s'\n", file);
        return -1;
    }
    int index = ext2_sub_find(mpctx, id);
    if (index < 0)
        index = ext2_sub_alloc(mpctx);
    if (index < 0) {
        munmap(p, h * stride);
        return -1;
    }
    cmd->overlay_map[id] = p;
    osd->external2.parts[index] = (struct sub_bitmap) {
        .bitmap = p,
        .stride = stride,
        .x = x, .y = y,
        .w = w, .h = h,
        .dw = w, .dh = h,
    };
    osd->external2.bitmap_id = osd->external2.bitmap_pos_id = 1;
    osd->external2.format = SUBBITMAP_RGBA;
    osd->want_redraw = true;
    return 0;
}

static void overlay_remove(struct MPContext *mpctx, int id)
{
    struct command_ctx *cmd = mpctx->command_ctx;
    struct osd_state *osd = mpctx->osd;
    int index = ext2_sub_find(mpctx, id);
    if (index >= 0) {
        struct sub_bitmaps *sub = &osd->external2;
        struct sub_bitmap *part = &sub->parts[index];
        munmap(part->bitmap, part->h * part->stride);
        MP_TARRAY_REMOVE_AT(sub->parts, sub->num_parts, index);
        cmd->overlay_map[id] = NULL;
        sub->bitmap_id = sub->bitmap_pos_id = 1;
    }
}

static void overlay_uninit(struct MPContext *mpctx)
{
    for (int id = 0; id < OVERLAY_MAX_ID; id++)
        overlay_remove(mpctx, id);
}

#else

static void overlay_uninit(struct MPContext *mpctx){}

#endif

void run_command(MPContext *mpctx, mp_cmd_t *cmd)
{
    struct MPOpts *opts = mpctx->opts;
    int osd_duration = opts->osd_duration;
    bool auto_osd = cmd->on_osd == MP_ON_OSD_AUTO;
    bool msg_osd = auto_osd || (cmd->on_osd & MP_ON_OSD_MSG);
    bool bar_osd = auto_osd || (cmd->on_osd & MP_ON_OSD_BAR);
    bool msg_or_nobar_osd = msg_osd && !(auto_osd && opts->osd_bar_visible);
    int osdl = msg_osd ? 1 : OSD_LEVEL_INVISIBLE;

    if (!cmd->raw_args) {
        for (int n = 0; n < cmd->nargs; n++) {
            if (cmd->args[n].type.type == CONF_TYPE_STRING) {
                cmd->args[n].v.s =
                    mp_property_expand_string(mpctx, cmd->args[n].v.s);
                if (!cmd->args[n].v.s)
                    return;
                talloc_steal(cmd, cmd->args[n].v.s);
            }
        }
    }

    switch (cmd->id) {
    case MP_CMD_SEEK: {
        double v = cmd->args[0].v.d * cmd->scale;
        int abs = cmd->args[1].v.i;
        int exact = cmd->args[2].v.i;
        if (abs == 2) {   // Absolute seek to a timestamp in seconds
            queue_seek(mpctx, MPSEEK_ABSOLUTE, v, exact);
            set_osd_function(mpctx,
                             v > get_current_time(mpctx) ? OSD_FFW : OSD_REW);
        } else if (abs) {           /* Absolute seek by percentage */
            queue_seek(mpctx, MPSEEK_FACTOR, v / 100.0, exact);
            set_osd_function(mpctx, OSD_FFW); // Direction isn't set correctly
        } else {
            queue_seek(mpctx, MPSEEK_RELATIVE, v, exact);
            set_osd_function(mpctx, (v > 0) ? OSD_FFW : OSD_REW);
        }
        if (bar_osd)
            mpctx->add_osd_seek_info |= OSD_SEEK_INFO_BAR;
        if (msg_or_nobar_osd)
            mpctx->add_osd_seek_info |= OSD_SEEK_INFO_TEXT;
        break;
    }

    case MP_CMD_SET: {
        int r = mp_property_do(cmd->args[0].v.s, M_PROPERTY_SET_STRING,
                               cmd->args[1].v.s, mpctx);
        if (r == M_PROPERTY_OK || r == M_PROPERTY_UNAVAILABLE) {
            show_property_osd(mpctx, cmd->args[0].v.s, cmd->on_osd);
        } else if (r == M_PROPERTY_UNKNOWN) {
            set_osd_msg(mpctx, OSD_MSG_TEXT, osdl, osd_duration,
                        "Unknown property: '%s'", cmd->args[0].v.s);
        } else if (r <= 0) {
            set_osd_msg(mpctx, OSD_MSG_TEXT, osdl, osd_duration,
                        "Failed to set property '%s' to '%s'",
                        cmd->args[0].v.s, cmd->args[1].v.s);
        }
        break;
    }

    case MP_CMD_ADD:
    case MP_CMD_CYCLE:
    {
        struct m_property_switch_arg s = {
            .inc = 1,
            .wrap = cmd->id == MP_CMD_CYCLE,
        };
        if (cmd->args[1].v.d)
            s.inc = cmd->args[1].v.d * cmd->scale;
        int r = mp_property_do(cmd->args[0].v.s, M_PROPERTY_SWITCH, &s, mpctx);
        if (r == M_PROPERTY_OK || r == M_PROPERTY_UNAVAILABLE) {
            show_property_osd(mpctx, cmd->args[0].v.s, cmd->on_osd);
        } else if (r == M_PROPERTY_UNKNOWN) {
            set_osd_msg(mpctx, OSD_MSG_TEXT, osdl, osd_duration,
                        "Unknown property: '%s'", cmd->args[0].v.s);
        } else if (r <= 0) {
            set_osd_msg(mpctx, OSD_MSG_TEXT, osdl, osd_duration,
                        "Failed to increment property '%s' by %g",
                        cmd->args[0].v.s, s.inc);
        }
        break;
    }

    case MP_CMD_GET_PROPERTY: {
        char *tmp;
        int r = mp_property_do(cmd->args[0].v.s, M_PROPERTY_GET_STRING,
                               &tmp, mpctx);
        if (r <= 0) {
            mp_msg(MSGT_CPLAYER, MSGL_WARN,
                   "Failed to get value of property '%s'.\n",
                   cmd->args[0].v.s);
            mp_msg(MSGT_GLOBAL, MSGL_INFO, "ANS_ERROR=%s\n",
                   property_error_string(r));
            break;
        }
        mp_msg(MSGT_GLOBAL, MSGL_INFO, "ANS_%s=%s\n",
               cmd->args[0].v.s, tmp);
        talloc_free(tmp);
        break;
    }

    case MP_CMD_SPEED_MULT: {
        double v = cmd->args[0].v.d * cmd->scale;
        v *= mpctx->opts->playback_speed;
        mp_property_do("speed", M_PROPERTY_SET, &v, mpctx);
        show_property_osd(mpctx, "speed", cmd->on_osd);
        break;
    }

    case MP_CMD_FRAME_STEP:
        add_step_frame(mpctx, 1);
        break;

    case MP_CMD_FRAME_BACK_STEP:
        add_step_frame(mpctx, -1);
        break;

    case MP_CMD_QUIT:
        mpctx->stop_play = PT_QUIT;
        mpctx->quit_custom_rc = cmd->args[0].v.i;
        mpctx->has_quit_custom_rc = true;
        break;

    case MP_CMD_QUIT_WATCH_LATER:
        mp_write_watch_later_conf(mpctx);
        mpctx->stop_play = PT_QUIT;
        mpctx->quit_player_rc = 0;
        break;

    case MP_CMD_PLAYLIST_NEXT:
    case MP_CMD_PLAYLIST_PREV:
    {
        int dir = cmd->id == MP_CMD_PLAYLIST_PREV ? -1 : +1;
        int force = cmd->args[0].v.i;

        struct playlist_entry *e = mp_next_file(mpctx, dir, force);
        if (!e && !force)
            break;
        mpctx->playlist->current = e;
        mpctx->playlist->current_was_replaced = false;
        mpctx->stop_play = PT_CURRENT_ENTRY;
        break;
    }

    case MP_CMD_SUB_STEP:
    case MP_CMD_SUB_SEEK:
        if (mpctx->osd->dec_sub) {
            double a[2];
            a[0] = mpctx->video_pts - mpctx->osd->video_offset + opts->sub_delay;
            a[1] = cmd->args[0].v.i;
            if (sub_control(mpctx->osd->dec_sub, SD_CTRL_SUB_STEP, a) > 0) {
                if (cmd->id == MP_CMD_SUB_STEP) {
                    opts->sub_delay += a[0];
                    osd_changed_all(mpctx->osd);
                    set_osd_tmsg(mpctx, OSD_MSG_SUB_DELAY, osdl, osd_duration,
                                 "Sub delay: %d ms", ROUND(opts->sub_delay * 1000));
                } else {
                    // We can easily get stuck by failing to seek to the video
                    // frame which actually shows the sub first (because video
                    // frame PTS and sub PTS rarely match exactly). Add some
                    // rounding for the mess of it.
                    a[0] += 0.01 * (a[1] > 0 ? 1 : -1);
                    queue_seek(mpctx, MPSEEK_RELATIVE, a[0], 1);
                    set_osd_function(mpctx, (a[0] > 0) ? OSD_FFW : OSD_REW);
                    if (bar_osd)
                        mpctx->add_osd_seek_info |= OSD_SEEK_INFO_BAR;
                    if (msg_or_nobar_osd)
                        mpctx->add_osd_seek_info |= OSD_SEEK_INFO_TEXT;
                }
            }
        }
        break;

    case MP_CMD_OSD: {
        int v = cmd->args[0].v.i;
        int max = (opts->term_osd && !mpctx->video_out) ? MAX_TERM_OSD_LEVEL
                                                        : MAX_OSD_LEVEL;
        if (opts->osd_level > max)
            opts->osd_level = max;
        if (v < 0)
            opts->osd_level = (opts->osd_level + 1) % (max + 1);
        else
            opts->osd_level = v > max ? max : v;
        if (msg_osd && opts->osd_level <= 1)
            set_osd_tmsg(mpctx, OSD_MSG_OSD_STATUS, 0, osd_duration,
                         "OSD: %s", opts->osd_level ? "yes" : "no");
        else
            rm_osd_msg(mpctx, OSD_MSG_OSD_STATUS);
        break;
    }

    case MP_CMD_PRINT_TEXT: {
        mp_msg(MSGT_GLOBAL, MSGL_INFO, "%s\n", cmd->args[0].v.s);
        break;
    }

    case MP_CMD_SHOW_TEXT: {
        // if no argument supplied use default osd_duration, else <arg> ms.
        set_osd_msg(mpctx, OSD_MSG_TEXT, cmd->args[2].v.i,
                    (cmd->args[1].v.i < 0 ? osd_duration : cmd->args[1].v.i),
                    "%s", cmd->args[0].v.s);
        break;
    }

    case MP_CMD_LOADFILE: {
        char *filename = cmd->args[0].v.s;
        bool append = cmd->args[1].v.i;

        if (!append)
            playlist_clear(mpctx->playlist);

        playlist_add(mpctx->playlist, playlist_entry_new(filename));

        if (!append)
            mp_set_playlist_entry(mpctx, mpctx->playlist->first);
        break;
    }

    case MP_CMD_LOADLIST: {
        char *filename = cmd->args[0].v.s;
        bool append = cmd->args[1].v.i;
        struct playlist *pl = playlist_parse_file(filename, opts);
        if (pl) {
            if (!append)
                playlist_clear(mpctx->playlist);
            playlist_transfer_entries(mpctx->playlist, pl);
            talloc_free(pl);

            if (!append && mpctx->playlist->first) {
                struct playlist_entry *e =
                    mp_resume_playlist(mpctx->playlist, opts);
                mp_set_playlist_entry(mpctx, e ? e : mpctx->playlist->first);
            }
        } else {
            mp_tmsg(MSGT_CPLAYER, MSGL_ERR,
                    "\nUnable to load playlist %s.\n", filename);
        }
        break;
    }

    case MP_CMD_PLAYLIST_CLEAR: {
        // Supposed to clear the playlist, except the currently played item.
        if (mpctx->playlist->current_was_replaced)
            mpctx->playlist->current = NULL;
        while (mpctx->playlist->first) {
            struct playlist_entry *e = mpctx->playlist->first;
            if (e == mpctx->playlist->current) {
                e = e->next;
                if (!e)
                    break;
            }
            playlist_remove(mpctx->playlist, e);
        }
        break;
    }

    case MP_CMD_PLAYLIST_REMOVE: {
        struct playlist_entry *e = playlist_entry_from_index(mpctx->playlist,
                                                             cmd->args[0].v.i);
        if (e) {
            // Can't play a removed entry
            if (mpctx->playlist->current == e)
                mpctx->stop_play = PT_CURRENT_ENTRY;
            playlist_remove(mpctx->playlist, e);
        }
        break;
    }

    case MP_CMD_PLAYLIST_MOVE: {
        struct playlist_entry *e1 = playlist_entry_from_index(mpctx->playlist,
                                                              cmd->args[0].v.i);
        struct playlist_entry *e2 = playlist_entry_from_index(mpctx->playlist,
                                                              cmd->args[1].v.i);
        if (e1) {
            playlist_move(mpctx->playlist, e1, e2);
        }
        break;
    }

    case MP_CMD_STOP:
        // Go back to the starting point.
        mpctx->stop_play = PT_STOP;
        break;

    case MP_CMD_SHOW_PROGRESS:
        mpctx->add_osd_seek_info |=
                (msg_osd ? OSD_SEEK_INFO_TEXT : 0) |
                (bar_osd ? OSD_SEEK_INFO_BAR : 0);
        break;

#ifdef CONFIG_RADIO
    case MP_CMD_RADIO_STEP_CHANNEL:
        if (mpctx->stream && mpctx->stream->type == STREAMTYPE_RADIO) {
            int v = cmd->args[0].v.i;
            if (v > 0)
                radio_step_channel(mpctx->stream, RADIO_CHANNEL_HIGHER);
            else
                radio_step_channel(mpctx->stream, RADIO_CHANNEL_LOWER);
            if (radio_get_channel_name(mpctx->stream)) {
                set_osd_tmsg(mpctx, OSD_MSG_RADIO_CHANNEL, osdl, osd_duration,
                             "Channel: %s",
                             radio_get_channel_name(mpctx->stream));
            }
        }
        break;

    case MP_CMD_RADIO_SET_CHANNEL:
        if (mpctx->stream && mpctx->stream->type == STREAMTYPE_RADIO) {
            radio_set_channel(mpctx->stream, cmd->args[0].v.s);
            if (radio_get_channel_name(mpctx->stream)) {
                set_osd_tmsg(mpctx, OSD_MSG_RADIO_CHANNEL, osdl, osd_duration,
                             "Channel: %s",
                             radio_get_channel_name(mpctx->stream));
            }
        }
        break;

    case MP_CMD_RADIO_SET_FREQ:
        if (mpctx->stream && mpctx->stream->type == STREAMTYPE_RADIO)
            radio_set_freq(mpctx->stream, cmd->args[0].v.f);
        break;

    case MP_CMD_RADIO_STEP_FREQ:
        if (mpctx->stream && mpctx->stream->type == STREAMTYPE_RADIO)
            radio_step_freq(mpctx->stream, cmd->args[0].v.f);
        break;
#endif

#ifdef CONFIG_TV
    case MP_CMD_TV_START_SCAN:
        if (get_tvh(mpctx))
            tv_start_scan(get_tvh(mpctx), 1);
        break;
    case MP_CMD_TV_SET_FREQ:
        if (get_tvh(mpctx))
            tv_set_freq(get_tvh(mpctx), cmd->args[0].v.f * 16.0);
#ifdef CONFIG_PVR
        else if (mpctx->stream && mpctx->stream->type == STREAMTYPE_PVR) {
            pvr_set_freq(mpctx->stream, ROUND(cmd->args[0].v.f));
            set_osd_msg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration, "%s: %s",
                        pvr_get_current_channelname(mpctx->stream),
                        pvr_get_current_stationname(mpctx->stream));
        }
#endif /* CONFIG_PVR */
        break;

    case MP_CMD_TV_STEP_FREQ:
        if (get_tvh(mpctx))
            tv_step_freq(get_tvh(mpctx), cmd->args[0].v.f * 16.0);
#ifdef CONFIG_PVR
        else if (mpctx->stream && mpctx->stream->type == STREAMTYPE_PVR) {
            pvr_force_freq_step(mpctx->stream, ROUND(cmd->args[0].v.f));
            set_osd_msg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration, "%s: f %d",
                        pvr_get_current_channelname(mpctx->stream),
                        pvr_get_current_frequency(mpctx->stream));
        }
#endif /* CONFIG_PVR */
        break;

    case MP_CMD_TV_SET_NORM:
        if (get_tvh(mpctx))
            tv_set_norm(get_tvh(mpctx), cmd->args[0].v.s);
        break;

    case MP_CMD_TV_STEP_CHANNEL:
        if (get_tvh(mpctx)) {
            int v = cmd->args[0].v.i;
            if (v > 0) {
                tv_step_channel(get_tvh(mpctx), TV_CHANNEL_HIGHER);
            } else {
                tv_step_channel(get_tvh(mpctx), TV_CHANNEL_LOWER);
            }
            if (tv_channel_list) {
                set_osd_tmsg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration,
                             "Channel: %s", tv_channel_current->name);
            }
        }
#ifdef CONFIG_PVR
        else if (mpctx->stream &&
                 mpctx->stream->type == STREAMTYPE_PVR) {
            pvr_set_channel_step(mpctx->stream, cmd->args[0].v.i);
            set_osd_msg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration, "%s: %s",
                        pvr_get_current_channelname(mpctx->stream),
                        pvr_get_current_stationname(mpctx->stream));
        }
#endif /* CONFIG_PVR */
#ifdef CONFIG_DVBIN
        if (mpctx->stream->type == STREAMTYPE_DVB) {
            int dir;
            int v = cmd->args[0].v.i;

            mpctx->last_dvb_step = v;
            if (v > 0)
                dir = DVB_CHANNEL_HIGHER;
            else
                dir = DVB_CHANNEL_LOWER;


            if (dvb_step_channel(mpctx->stream, dir)) {
                mpctx->stop_play = PT_NEXT_ENTRY;
                mpctx->dvbin_reopen = 1;
            }
        }
#endif /* CONFIG_DVBIN */
        break;

    case MP_CMD_TV_SET_CHANNEL:
        if (get_tvh(mpctx)) {
            tv_set_channel(get_tvh(mpctx), cmd->args[0].v.s);
            if (tv_channel_list) {
                set_osd_tmsg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration,
                             "Channel: %s", tv_channel_current->name);
            }
        }
#ifdef CONFIG_PVR
        else if (mpctx->stream && mpctx->stream->type == STREAMTYPE_PVR) {
            pvr_set_channel(mpctx->stream, cmd->args[0].v.s);
            set_osd_msg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration, "%s: %s",
                        pvr_get_current_channelname(mpctx->stream),
                        pvr_get_current_stationname(mpctx->stream));
        }
#endif /* CONFIG_PVR */
        break;

#ifdef CONFIG_DVBIN
    case MP_CMD_DVB_SET_CHANNEL:
        if (mpctx->stream->type == STREAMTYPE_DVB) {
            mpctx->last_dvb_step = 1;

            if (dvb_set_channel(mpctx->stream, cmd->args[1].v.i,
                                cmd->args[0].v.i)) {
                mpctx->stop_play = PT_NEXT_ENTRY;
                mpctx->dvbin_reopen = 1;
            }
        }
        break;
#endif /* CONFIG_DVBIN */

    case MP_CMD_TV_LAST_CHANNEL:
        if (get_tvh(mpctx)) {
            tv_last_channel(get_tvh(mpctx));
            if (tv_channel_list) {
                set_osd_tmsg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration,
                             "Channel: %s", tv_channel_current->name);
            }
        }
#ifdef CONFIG_PVR
        else if (mpctx->stream && mpctx->stream->type == STREAMTYPE_PVR) {
            pvr_set_lastchannel(mpctx->stream);
            set_osd_msg(mpctx, OSD_MSG_TV_CHANNEL, osdl, osd_duration, "%s: %s",
                        pvr_get_current_channelname(mpctx->stream),
                        pvr_get_current_stationname(mpctx->stream));
        }
#endif /* CONFIG_PVR */
        break;

    case MP_CMD_TV_STEP_NORM:
        if (get_tvh(mpctx))
            tv_step_norm(get_tvh(mpctx));
        break;

    case MP_CMD_TV_STEP_CHANNEL_LIST:
        if (get_tvh(mpctx))
            tv_step_chanlist(get_tvh(mpctx));
        break;
#endif /* CONFIG_TV */

    case MP_CMD_SUB_ADD:
        mp_add_subtitles(mpctx, cmd->args[0].v.s);
        break;

    case MP_CMD_SUB_REMOVE: {
        struct track *sub = mp_track_by_tid(mpctx, STREAM_SUB, cmd->args[0].v.i);
        if (sub)
            mp_remove_track(mpctx, sub);
        break;
    }

    case MP_CMD_SUB_RELOAD: {
        struct track *sub = mp_track_by_tid(mpctx, STREAM_SUB, cmd->args[0].v.i);
        if (sub && sub->is_external && sub->external_filename) {
            struct track *nsub = mp_add_subtitles(mpctx, sub->external_filename);
            if (nsub) {
                mp_remove_track(mpctx, sub);
                mp_switch_track(mpctx, nsub->type, nsub);
            }
        }
        break;
    }

    case MP_CMD_SCREENSHOT:
        screenshot_request(mpctx, cmd->args[0].v.i, cmd->args[1].v.i, msg_osd);
        break;

    case MP_CMD_SCREENSHOT_TO_FILE:
        screenshot_to_file(mpctx, cmd->args[0].v.s, cmd->args[1].v.i, msg_osd);
        break;

    case MP_CMD_RUN:
#ifndef __MINGW32__
        if (!fork()) {
            execl("/bin/sh", "sh", "-c", cmd->args[0].v.s, NULL);
            exit(0);
        }
#endif
        break;

    case MP_CMD_KEYDOWN_EVENTS:
        mp_input_put_key(mpctx->input, cmd->args[0].v.i);
        break;

    case MP_CMD_ENABLE_INPUT_SECTION:
        mp_input_enable_section(mpctx->input, cmd->args[0].v.s,
                                cmd->args[1].v.i == 1 ? MP_INPUT_EXCLUSIVE : 0);
        break;

    case MP_CMD_DISABLE_INPUT_SECTION:
        mp_input_disable_section(mpctx->input, cmd->args[0].v.s);
        break;

    case MP_CMD_VO_CMDLINE:
        if (mpctx->video_out) {
            char *s = cmd->args[0].v.s;
            mp_msg(MSGT_CPLAYER, MSGL_INFO, "Setting vo cmd line to '%s'.\n",
                   s);
            if (vo_control(mpctx->video_out, VOCTRL_SET_COMMAND_LINE, s) > 0) {
                set_osd_msg(mpctx, OSD_MSG_TEXT, osdl, osd_duration, "vo='%s'", s);
            } else {
                set_osd_msg(mpctx, OSD_MSG_TEXT, osdl, osd_duration, "Failed!");
            }
        }
        break;

    case MP_CMD_AF:
        edit_filters_osd(mpctx, STREAM_AUDIO, cmd->args[0].v.s,
                         cmd->args[1].v.s, msg_osd);
        break;

    case MP_CMD_VF:
        edit_filters_osd(mpctx, STREAM_VIDEO, cmd->args[0].v.s,
                         cmd->args[1].v.s, msg_osd);
        break;

    case MP_CMD_SCRIPT_DISPATCH:
        if (mpctx->lua_ctx) {
#ifdef CONFIG_LUA
            mp_lua_script_dispatch(mpctx, cmd->args[0].v.s, cmd->args[1].v.i,
                            cmd->key_up_follows ? "keyup_follows" : "press");
#endif
        }
        break;

#ifdef HAVE_SYS_MMAN_H
    case MP_CMD_OVERLAY_ADD:
        overlay_add(mpctx,
                    cmd->args[0].v.i, cmd->args[1].v.i, cmd->args[2].v.i,
                    cmd->args[3].v.s, cmd->args[4].v.i, cmd->args[5].v.s,
                    cmd->args[6].v.i, cmd->args[7].v.i, cmd->args[8].v.i);
        break;

    case MP_CMD_OVERLAY_REMOVE:
        overlay_remove(mpctx, cmd->args[0].v.i);
        break;
#endif

    case MP_CMD_COMMAND_LIST: {
        for (struct mp_cmd *sub = cmd->args[0].v.p; sub; sub = sub->queue_next)
            run_command(mpctx, sub);
        break;
    }

    case MP_CMD_IGNORE:
        break;

    default:
        mp_msg(MSGT_CPLAYER, MSGL_V,
               "Received unknown cmd %s\n", cmd->name);
    }

    switch (cmd->pausing) {
    case 1:     // "pausing"
        pause_player(mpctx);
        break;
    case 3:     // "pausing_toggle"
        if (opts->pause)
            unpause_player(mpctx);
        else
            pause_player(mpctx);
        break;
    }
}

void command_uninit(struct MPContext *mpctx)
{
    overlay_uninit(mpctx);
    talloc_free(mpctx->command_ctx);
    mpctx->command_ctx = NULL;
}

void command_init(struct MPContext *mpctx)
{
    mpctx->command_ctx = talloc_zero(NULL, struct command_ctx);
}

// Notify that a property might have changed.
void mp_notify_property(struct MPContext *mpctx, const char *property)
{
    mp_notify(mpctx, MP_EVENT_PROPERTY, (void *)property);
}

void mp_notify(struct MPContext *mpctx, enum mp_event event, void *arg)
{
    struct command_ctx *ctx = mpctx->command_ctx;
    ctx->events |= 1u << event;
}

static void handle_script_event(struct MPContext *mpctx, const char *name,
                                const char *arg)
{
#ifdef CONFIG_LUA
    mp_lua_event(mpctx, name, arg);
#endif
}

void mp_flush_events(struct MPContext *mpctx)
{
    struct command_ctx *ctx = mpctx->command_ctx;

    ctx->events |= (1u << MP_EVENT_TICK);

    for (int n = 0; n < 16; n++) {
        enum mp_event event = n;
        unsigned mask = 1 << event;
        if (ctx->events & mask) {
            // The event handler could set event flags again; in this case let
            // the next mp_flush_events() call handle it to avoid infinite loops.
            ctx->events &= ~mask;
            const char *name = NULL;
            switch (event) {
            case MP_EVENT_TICK:             name = "tick"; break;
            case MP_EVENT_TRACKS_CHANGED:   name = "track-layout"; break;
            case MP_EVENT_START_FILE:       name = "start"; break;
            case MP_EVENT_END_FILE:         name = "end"; break;
            default: ;
            }
            if (name)
                handle_script_event(mpctx, name, "");
        }
    }
}
