
/* Modify video timing to match the audio timeline. There are two main
 * reasons this is needed. First, video and audio can start from different
 * positions at beginning of file or after a seek (MPlayer starts both
 * immediately even if they have different pts). Second, the file can have
 * audio timestamps that are inconsistent with the duration of the audio
 * packets, for example two consecutive timestamp values differing by
 * one second but only a packet with enough samples for half a second
 * of playback between them.
 */
static void adjust_sync(struct MPContext *mpctx, double frame_time)
{
    struct MPOpts *opts = mpctx->opts;

    if (!mpctx->sh_audio || mpctx->syncing_audio)
        return;

    double a_pts = written_audio_pts(mpctx) - mpctx->delay;
    double v_pts = mpctx->sh_video->pts;
    double av_delay = a_pts - v_pts;
    // Try to sync vo_flip() so it will *finish* at given time
    av_delay += mpctx->last_vo_flip_duration;
    av_delay -= mpctx->audio_delay;   // This much pts difference is desired

    double change = av_delay * 0.1;
    double max_change = opts->default_max_pts_correction >= 0 ?
                        opts->default_max_pts_correction : frame_time * 0.1;
    if (change < -max_change)
        change = -max_change;
    else if (change > max_change)
        change = max_change;
    mpctx->delay += change;
    mpctx->total_avsync_change += change;
}

static void update_fps(struct MPContext *mpctx)
{
#ifdef CONFIG_ENCODING
    struct sh_video *sh_video = mpctx->sh_video;
    if (mpctx->encode_lavc_ctx && sh_video)
        encode_lavc_set_video_fps(mpctx->encode_lavc_ctx, sh_video->fps);
#endif
}

static void recreate_video_filters(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct sh_video *sh_video = mpctx->sh_video;
    assert(sh_video);

    vf_uninit_filter_chain(sh_video->vfilter);

    char *vf_arg[] = {
        "_oldargs_", (char *)mpctx->video_out, NULL
    };
    sh_video->vfilter = vf_open_filter(opts, NULL, "vo", vf_arg);

    sh_video->vfilter = append_filters(sh_video->vfilter, opts->vf_settings);

    struct vf_instance *vf = sh_video->vfilter;
    mpctx->osd->render_subs_in_filter
        = vf->control(vf, VFCTRL_INIT_OSD, NULL) == VO_TRUE;
}

int reinit_video_filters(struct MPContext *mpctx)
{
    struct sh_video *sh_video = mpctx->sh_video;

    if (!sh_video)
        return -2;

    recreate_video_filters(mpctx);
    video_reinit_vo(sh_video);

    return sh_video->vf_initialized > 0 ? 0 : -1;
}

int reinit_video_chain(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    assert(!(mpctx->initialized_flags & INITIALIZED_VCODEC));
    init_demux_stream(mpctx, STREAM_VIDEO);
    sh_video_t *sh_video = mpctx->sh_video;
    if (!sh_video)
        goto no_video;

    MP_VERBOSE(mpctx, "[V] fourcc:0x%X  size:%dx%d  fps:%5.3f\n",
               mpctx->sh_video->format,
               mpctx->sh_video->disp_w, mpctx->sh_video->disp_h,
               mpctx->sh_video->fps);
    if (opts->force_fps)
        mpctx->sh_video->fps = opts->force_fps;
    update_fps(mpctx);

    if (!mpctx->sh_video->fps && !opts->force_fps && !opts->correct_pts) {
        MP_ERR(mpctx, "FPS not specified in the "
               "header or invalid, use the -fps option.\n");
    }

    double ar = -1.0;
    //================== Init VIDEO (codec & libvo) ==========================
    if (!opts->fixed_vo || !(mpctx->initialized_flags & INITIALIZED_VO)) {
        mpctx->video_out = init_best_video_out(mpctx->global, mpctx->input,
                                               mpctx->encode_lavc_ctx);
        if (!mpctx->video_out) {
            MP_FATAL(mpctx, "Error opening/initializing "
                    "the selected video_out (-vo) device.\n");
            goto err_out;
        }
        mpctx->mouse_cursor_visible = true;
        mpctx->initialized_flags |= INITIALIZED_VO;
    }

    // dynamic allocation only to make stheader.h lighter
    talloc_free(sh_video->hwdec_info);
    sh_video->hwdec_info = talloc_zero(sh_video, struct mp_hwdec_info);
    vo_control(mpctx->video_out, VOCTRL_GET_HWDEC_INFO, sh_video->hwdec_info);

    vo_update_window_title(mpctx);

    if (stream_control(mpctx->sh_video->gsh->demuxer->stream,
                       STREAM_CTRL_GET_ASPECT_RATIO, &ar) != STREAM_UNSUPPORTED)
        mpctx->sh_video->stream_aspect = ar;

    recreate_video_filters(mpctx);

    init_best_video_codec(sh_video, opts->video_decoders);

    if (!sh_video->initialized)
        goto err_out;

    mpctx->initialized_flags |= INITIALIZED_VCODEC;

    bool saver_state = opts->pause || !opts->stop_screensaver;
    vo_control(mpctx->video_out, saver_state ? VOCTRL_RESTORE_SCREENSAVER
                                             : VOCTRL_KILL_SCREENSAVER, NULL);

    vo_control(mpctx->video_out, mpctx->paused ? VOCTRL_PAUSE
                                               : VOCTRL_RESUME, NULL);

    sh_video->last_pts = MP_NOPTS_VALUE;
    sh_video->num_buffered_pts = 0;
    sh_video->next_frame_time = 0;
    mpctx->last_vf_reconfig_count = 0;
    mpctx->restart_playback = true;
    mpctx->sync_audio_to_video = !sh_video->gsh->attached_picture;
    mpctx->delay = 0;
    mpctx->vo_pts_history_seek_ts++;

    vo_seek_reset(mpctx->video_out);
    reset_subtitles(mpctx);

    return 1;

err_out:
no_video:
    uninit_player(mpctx, INITIALIZED_VCODEC | (opts->force_vo ? 0 : INITIALIZED_VO));
    cleanup_demux_stream(mpctx, STREAM_VIDEO);
    handle_force_window(mpctx, true);
    MP_INFO(mpctx, "Video: no video\n");
    return 0;
}

// Try to refresh the video by doing a precise seek to the currently displayed
// frame. This can go wrong in all sorts of ways, so use sparingly.
void mp_force_video_refresh(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;

    // If not paused, the next frame should come soon enough.
    if (opts->pause && mpctx->last_vo_pts != MP_NOPTS_VALUE)
        queue_seek(mpctx, MPSEEK_ABSOLUTE, mpctx->last_vo_pts, 1);
}

static void add_frame_pts(struct MPContext *mpctx, double pts)
{
    if (pts == MP_NOPTS_VALUE || mpctx->hrseek_framedrop) {
        mpctx->vo_pts_history_seek_ts++; // mark discontinuity
        return;
    }
    for (int n = MAX_NUM_VO_PTS - 1; n >= 1; n--) {
        mpctx->vo_pts_history_seek[n] = mpctx->vo_pts_history_seek[n - 1];
        mpctx->vo_pts_history_pts[n] = mpctx->vo_pts_history_pts[n - 1];
    }
    mpctx->vo_pts_history_seek[0] = mpctx->vo_pts_history_seek_ts;
    mpctx->vo_pts_history_pts[0] = pts;
}

static double find_previous_pts(struct MPContext *mpctx, double pts)
{
    for (int n = 0; n < MAX_NUM_VO_PTS - 1; n++) {
        if (pts == mpctx->vo_pts_history_pts[n] &&
            mpctx->vo_pts_history_seek[n] != 0 &&
            mpctx->vo_pts_history_seek[n] == mpctx->vo_pts_history_seek[n + 1])
        {
            return mpctx->vo_pts_history_pts[n + 1];
        }
    }
    return MP_NOPTS_VALUE;
}

static double get_last_frame_pts(struct MPContext *mpctx)
{
    if (mpctx->vo_pts_history_seek[0] == mpctx->vo_pts_history_seek_ts)
        return mpctx->vo_pts_history_pts[0];
    return MP_NOPTS_VALUE;
}

static bool filter_output_queued_frame(struct MPContext *mpctx)
{
    struct sh_video *sh_video = mpctx->sh_video;
    struct vo *video_out = mpctx->video_out;

    struct mp_image *img = vf_chain_output_queued_frame(sh_video->vfilter);
    if (img)
        vo_queue_image(video_out, img);
    talloc_free(img);

    return !!img;
}

static bool load_next_vo_frame(struct MPContext *mpctx, bool eof)
{
    if (vo_get_buffered_frame(mpctx->video_out, eof) >= 0)
        return true;
    if (filter_output_queued_frame(mpctx))
        return true;
    return false;
}

static void init_filter_params(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct sh_video *sh_video = mpctx->sh_video;

    // Note that the video decoder already initializes the filter chain. This
    // might recreate the chain a second time, which is not very elegant, but
    // allows us to test whether enabling deinterlacing works with the current
    // video format and other filters.
    if (sh_video->vf_initialized != 1)
        return;

    if (sh_video->vf_reconfig_count <= mpctx->last_vf_reconfig_count) {
        if (opts->deinterlace >= 0) {
            mp_property_do("deinterlace", M_PROPERTY_SET, &opts->deinterlace,
                           mpctx);
        }
    }
    // Setting filter params has to be "stable" (no change if params already
    // set) - checking the reconfig count is just an optimization.
    mpctx->last_vf_reconfig_count = sh_video->vf_reconfig_count;
}

static void filter_video(struct MPContext *mpctx, struct mp_image *frame)
{
    struct sh_video *sh_video = mpctx->sh_video;

    init_filter_params(mpctx);

    frame->pts = sh_video->pts;
    mp_image_set_params(frame, sh_video->vf_input);
    vf_filter_frame(sh_video->vfilter, frame);
    filter_output_queued_frame(mpctx);
}


static struct demux_packet *video_read_frame(struct MPContext *mpctx)
{
    sh_video_t *sh_video = mpctx->sh_video;
    demuxer_t *demuxer = sh_video->gsh->demuxer;
    float pts1 = sh_video->last_pts;

    struct demux_packet *pkt = demux_read_packet(sh_video->gsh);
    if (!pkt)
        return NULL; // EOF

    if (pkt->pts != MP_NOPTS_VALUE)
        sh_video->last_pts = pkt->pts;

    float frame_time = sh_video->fps > 0 ? 1.0f / sh_video->fps : 0;

    // override frame_time for variable/unknown FPS formats:
    if (!mpctx->opts->force_fps) {
        double next_pts = demux_get_next_pts(sh_video->gsh);
        double d = next_pts == MP_NOPTS_VALUE ? sh_video->last_pts - pts1
                                              : next_pts - sh_video->last_pts;
        if (d >= 0) {
            if (demuxer->type == DEMUXER_TYPE_TV) {
                if (d > 0)
                    sh_video->fps = 1.0f / d;
                frame_time = d;
            } else {
                if ((int)sh_video->fps <= 1)
                    frame_time = d;
            }
        }
    }

    sh_video->pts = sh_video->last_pts;
    sh_video->next_frame_time = frame_time;
    return pkt;
}

static double update_video_nocorrect_pts(struct MPContext *mpctx)
{
    struct sh_video *sh_video = mpctx->sh_video;
    double frame_time = 0;
    while (1) {
        // In nocorrect-pts mode there is no way to properly time these frames
        if (load_next_vo_frame(mpctx, false))
            break;
        frame_time = sh_video->next_frame_time;
        if (mpctx->restart_playback)
            frame_time = 0;
        struct demux_packet *pkt = video_read_frame(mpctx);
        if (!pkt)
            return -1;
        if (mpctx->sh_audio)
            mpctx->delay -= frame_time;
        // video_read_frame can change fps (e.g. for ASF video)
        update_fps(mpctx);
        int framedrop_type = check_framedrop(mpctx, frame_time);

        void *decoded_frame = decode_video(sh_video, pkt, framedrop_type,
                                           sh_video->pts);
        talloc_free(pkt);
        if (decoded_frame) {
            filter_video(mpctx, decoded_frame);
        }
        break;
    }
    return frame_time;
}

static double update_video_attached_pic(struct MPContext *mpctx)
{
    struct sh_video *sh_video = mpctx->sh_video;

    // Try to decode the picture multiple times, until it is displayed.
    if (mpctx->video_out->hasframe)
        return -1;

    struct mp_image *decoded_frame =
            decode_video(sh_video, sh_video->gsh->attached_picture, 0, 0);
    if (decoded_frame)
        filter_video(mpctx, decoded_frame);
    load_next_vo_frame(mpctx, true);
    mpctx->sh_video->pts = MP_NOPTS_VALUE;
    return 0;
}

static void determine_frame_pts(struct MPContext *mpctx)
{
    struct sh_video *sh_video = mpctx->sh_video;
    struct MPOpts *opts = mpctx->opts;

    if (opts->user_pts_assoc_mode)
        sh_video->pts_assoc_mode = opts->user_pts_assoc_mode;
    else if (sh_video->pts_assoc_mode == 0) {
        if (mpctx->sh_video->gsh->demuxer->timestamp_type == TIMESTAMP_TYPE_PTS
            && sh_video->codec_reordered_pts != MP_NOPTS_VALUE)
            sh_video->pts_assoc_mode = 1;
        else
            sh_video->pts_assoc_mode = 2;
    } else {
        int probcount1 = sh_video->num_reordered_pts_problems;
        int probcount2 = sh_video->num_sorted_pts_problems;
        if (sh_video->pts_assoc_mode == 2) {
            int tmp = probcount1;
            probcount1 = probcount2;
            probcount2 = tmp;
        }
        if (probcount1 >= probcount2 * 1.5 + 2) {
            sh_video->pts_assoc_mode = 3 - sh_video->pts_assoc_mode;
            MP_VERBOSE(mpctx, "Switching to pts association mode "
                       "%d.\n", sh_video->pts_assoc_mode);
        }
    }
    sh_video->pts = sh_video->pts_assoc_mode == 1 ?
                    sh_video->codec_reordered_pts : sh_video->sorted_pts;
}

static double update_video(struct MPContext *mpctx, double endpts)
{
    struct sh_video *sh_video = mpctx->sh_video;
    struct vo *video_out = mpctx->video_out;
    sh_video->vfilter->control(sh_video->vfilter, VFCTRL_SET_OSD_OBJ,
                               mpctx->osd); // for vf_sub
    if (!mpctx->opts->correct_pts)
        return update_video_nocorrect_pts(mpctx);

    if (sh_video->gsh->attached_picture)
        return update_video_attached_pic(mpctx);

    double pts;

    while (1) {
        if (load_next_vo_frame(mpctx, false))
            break;
        pts = MP_NOPTS_VALUE;
        struct demux_packet *pkt = NULL;
        while (1) {
            pkt = demux_read_packet(mpctx->sh_video->gsh);
            if (!pkt || pkt->len)
                break;
            /* Packets with size 0 are assumed to not correspond to frames,
             * but to indicate the absence of a frame in formats like AVI
             * that must have packets at fixed timecode intervals. */
            talloc_free(pkt);
        }
        if (pkt)
            pts = pkt->pts;
        if (pts != MP_NOPTS_VALUE)
            pts += mpctx->video_offset;
        if (pts >= mpctx->hrseek_pts - .005)
            mpctx->hrseek_framedrop = false;
        int framedrop_type = mpctx->hrseek_active && mpctx->hrseek_framedrop ?
                             1 : check_framedrop(mpctx, -1);
        struct mp_image *decoded_frame =
            decode_video(sh_video, pkt, framedrop_type, pts);
        talloc_free(pkt);
        if (decoded_frame) {
            determine_frame_pts(mpctx);
            filter_video(mpctx, decoded_frame);
        } else if (!pkt) {
            if (!load_next_vo_frame(mpctx, true))
                return -1;
        }
        break;
    }

    if (!video_out->frame_loaded)
        return 0;

    pts = video_out->next_pts;
    if (pts == MP_NOPTS_VALUE) {
        MP_ERR(mpctx, "Video pts after filters MISSING\n");
        // Try to use decoder pts from before filters
        pts = sh_video->pts;
        if (pts == MP_NOPTS_VALUE)
            pts = sh_video->last_pts;
    }
    if (endpts == MP_NOPTS_VALUE || pts < endpts)
        add_frame_pts(mpctx, pts);
    if (mpctx->hrseek_active && pts < mpctx->hrseek_pts - .005) {
        vo_skip_frame(video_out);
        return 0;
    }
    mpctx->hrseek_active = false;
    sh_video->pts = pts;
    if (sh_video->last_pts == MP_NOPTS_VALUE)
        sh_video->last_pts = sh_video->pts;
    else if (sh_video->last_pts > sh_video->pts) {
        MP_WARN(mpctx, "Decreasing video pts: %f < %f\n",
                sh_video->pts, sh_video->last_pts);
        /* If the difference in pts is small treat it as jitter around the
         * right value (possibly caused by incorrect timestamp ordering) and
         * just show this frame immediately after the last one.
         * Treat bigger differences as timestamp resets and start counting
         * timing of later frames from the position of this one. */
        if (sh_video->last_pts - sh_video->pts > 0.5)
            sh_video->last_pts = sh_video->pts;
        else
            sh_video->pts = sh_video->last_pts;
    } else if (sh_video->pts >= sh_video->last_pts + 60) {
        // Assume a PTS difference >= 60 seconds is a discontinuity.
        MP_WARN(mpctx, "Jump in video pts: %f -> %f\n",
                sh_video->last_pts, sh_video->pts);
        sh_video->last_pts = sh_video->pts;
    }
    double frame_time = sh_video->pts - sh_video->last_pts;
    sh_video->last_pts = sh_video->pts;
    if (mpctx->sh_audio)
        mpctx->delay -= frame_time;
    return frame_time;
}
