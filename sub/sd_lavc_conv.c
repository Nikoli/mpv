/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/common.h>

#include "config.h"

#include "talloc.h"
#include "mpvcore/mp_msg.h"
#include "mpvcore/av_common.h"
#include "mpvcore/bstr.h"
#include "sd.h"

#if LIBAVCODEC_VERSION_MICRO >= 100
#define HAVE_AV_WEBVTT (LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 13, 100))
#else
#define HAVE_AV_WEBVTT 0
#endif

struct sd_lavc_priv {
    AVCodecContext *avctx;
};

static const char *get_lavc_format(const char *format)
{
    // For the hack involving parse_webvtt().
    if (format && strcmp(format, "webvtt-webm") == 0)
        format = "webvtt";
    return format;
}

static bool supports_format(const char *format)
{
    format = get_lavc_format(format);
    enum AVCodecID cid = mp_codec_to_av_codec_id(format);
    const AVCodecDescriptor *desc = avcodec_descriptor_get(cid);
    if (!desc)
        return false;
    // These are known to support AVSubtitleRect->ass.
    const char *whitelist[] =
        {"text", "ass", "ssa", "srt", "subrip", "microdvd", "mpl2",
         "jacosub", "pjs", "sami", "realtext", "subviewer", "subviewer1",
         "vplayer", "webvtt", 0};
    for (int n = 0; whitelist[n]; n++) {
        if (strcmp(format, whitelist[n]) == 0)
            return true;
    }
    return false;
}

// Disable style definitions generated by the libavcodec converter.
// We always want the user defined style instead.
static void disable_styles(bstr header)
{
    while (header.len) {
        int n = bstr_find(header, bstr0("\nStyle: "));
        if (n < 0)
            break;
        header.start[n + 1] = '#'; // turn into a comment
        header = bstr_cut(header, 2);
    }
}

static int init(struct sd *sd)
{
    struct sd_lavc_priv *priv = talloc_zero(NULL, struct sd_lavc_priv);
    AVCodecContext *avctx = NULL;
    const char *fmt = get_lavc_format(sd->codec);
    AVCodec *codec = avcodec_find_decoder(mp_codec_to_av_codec_id(fmt));
    if (!codec)
        goto error;
    avctx = avcodec_alloc_context3(codec);
    if (!avctx)
        goto error;
    avctx->extradata_size = sd->extradata_len;
    avctx->extradata = sd->extradata;
    if (avcodec_open2(avctx, codec, NULL) < 0)
        goto error;
    // Documented as "set by libavcodec", but there is no other way
    avctx->time_base = (AVRational) {1, 1000};
    priv->avctx = avctx;
    sd->priv = priv;
    sd->output_codec = "ssa";
    sd->output_extradata = avctx->subtitle_header;
    sd->output_extradata_len = avctx->subtitle_header_size;
    if (sd->output_extradata) {
        sd->output_extradata = talloc_memdup(sd, sd->output_extradata,
                                             sd->output_extradata_len);
        disable_styles((bstr){sd->output_extradata, sd->output_extradata_len});
    }
    return 0;

 error:
    mp_msg(MSGT_SUBREADER, MSGL_ERR,
           "Could not open libavcodec subtitle converter\n");
    av_free(avctx);
    talloc_free(priv);
    return -1;
}

#if HAVE_AV_WEBVTT

// FFmpeg WebVTT packets are pre-parsed in some way. The FFmpeg Matroska
// demuxer does this on its own. In order to free our demuxer_mkv.c from
// codec-specific crud, we do this here.
// Copied from libavformat/matroskadec.c (FFmpeg 818ebe9 / 2013-08-19)
// License: LGPL v2.1 or later
// Author header: The FFmpeg Project
// Modified in some ways.
static int parse_webvtt(AVPacket *in, AVPacket *pkt)
{
    uint8_t *id, *settings, *text, *buf;
    int id_len, settings_len, text_len;
    uint8_t *p, *q;
    int err;

    uint8_t *data = in->data;
    int data_len = in->size;

    if (data_len <= 0)
        return AVERROR_INVALIDDATA;

    p = data;
    q = data + data_len;

    id = p;
    id_len = -1;
    while (p < q) {
        if (*p == '\r' || *p == '\n') {
            id_len = p - id;
            if (*p == '\r')
                p++;
            break;
        }
        p++;
    }

    if (p >= q || *p != '\n')
        return AVERROR_INVALIDDATA;
    p++;

    settings = p;
    settings_len = -1;
    while (p < q) {
        if (*p == '\r' || *p == '\n') {
            settings_len = p - settings;
            if (*p == '\r')
                p++;
            break;
        }
        p++;
    }

    if (p >= q || *p != '\n')
        return AVERROR_INVALIDDATA;
    p++;

    text = p;
    text_len = q - p;
    while (text_len > 0) {
        const int len = text_len - 1;
        const uint8_t c = p[len];
        if (c != '\r' && c != '\n')
            break;
        text_len = len;
    }

    if (text_len <= 0)
        return AVERROR_INVALIDDATA;

    err = av_new_packet(pkt, text_len);
    if (err < 0)
        return AVERROR(err);

    memcpy(pkt->data, text, text_len);

    if (id_len > 0) {
        buf = av_packet_new_side_data(pkt,
                                      AV_PKT_DATA_WEBVTT_IDENTIFIER,
                                      id_len);
        if (buf == NULL) {
            av_free_packet(pkt);
            return AVERROR(ENOMEM);
        }
        memcpy(buf, id, id_len);
    }

    if (settings_len > 0) {
        buf = av_packet_new_side_data(pkt,
                                      AV_PKT_DATA_WEBVTT_SETTINGS,
                                      settings_len);
        if (buf == NULL) {
            av_free_packet(pkt);
            return AVERROR(ENOMEM);
        }
        memcpy(buf, settings, settings_len);
    }

    pkt->pts = in->pts;
    pkt->duration = in->duration;
    pkt->convergence_duration = in->convergence_duration;
    return 0;
}

#else

static int parse_webvtt(AVPacket *in, AVPacket *pkt)
{
    return -1;
}

#endif

static void decode(struct sd *sd, struct demux_packet *packet)
{
    struct sd_lavc_priv *priv = sd->priv;
    AVCodecContext *avctx = priv->avctx;
    double ts = av_q2d(av_inv_q(avctx->time_base));
    AVSubtitle sub = {0};
    AVPacket pkt;
    AVPacket parsed_pkt = {0};
    int ret, got_sub;

    mp_set_av_packet(&pkt, packet);
    pkt.pts = packet->pts == MP_NOPTS_VALUE ? AV_NOPTS_VALUE : packet->pts * ts;
    pkt.duration = packet->duration * ts;

    if (sd->codec && strcmp(sd->codec, "webvtt-webm") == 0) {
        if (parse_webvtt(&pkt, &parsed_pkt) < 0) {
            mp_msg(MSGT_OSD, MSGL_ERR, "Error parsing subtitle\n");
            goto done;
        }
        pkt = parsed_pkt;
    }

    ret = avcodec_decode_subtitle2(avctx, &sub, &got_sub, &pkt);
    if (ret < 0) {
        mp_msg(MSGT_OSD, MSGL_ERR, "Error decoding subtitle\n");
    } else if (got_sub) {
        for (int i = 0; i < sub.num_rects; i++) {
            char *ass_line = sub.rects[i]->ass;
            if (!ass_line)
                break;
            // This might contain embedded timestamps, using the "old" ffmpeg
            // ASS packet format, in which case pts/duration might be ignored
            // at a later point.
            sd_conv_add_packet(sd, ass_line, strlen(ass_line),
                               packet->pts, packet->duration);
        }
    }

done:
    avsubtitle_free(&sub);
    av_free_packet(&parsed_pkt);
}

static void reset(struct sd *sd)
{
    struct sd_lavc_priv *priv = sd->priv;

    avcodec_flush_buffers(priv->avctx);
    sd_conv_def_reset(sd);
}

static void uninit(struct sd *sd)
{
    struct sd_lavc_priv *priv = sd->priv;

    avcodec_close(priv->avctx);
    av_free(priv->avctx);
    talloc_free(priv);
}

const struct sd_functions sd_lavc_conv = {
    .name = "lavc_conv",
    .supports_format = supports_format,
    .init = init,
    .decode = decode,
    .get_converted = sd_conv_def_get_converted,
    .reset = reset,
    .uninit = uninit,
};
