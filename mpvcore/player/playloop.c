
static double get_relative_time(struct MPContext *mpctx)
{
    int64_t new_time = mp_time_us();
    int64_t delta = new_time - mpctx->last_time;
    mpctx->last_time = new_time;
    return delta * 0.000001;
}

static double rel_time_to_abs(struct MPContext *mpctx, struct m_rel_time t,
                              double fallback_time)
{
    double length = get_time_length(mpctx);
    switch (t.type) {
    case REL_TIME_ABSOLUTE:
        return t.pos;
    case REL_TIME_NEGATIVE:
        if (length != 0)
            return FFMAX(length - t.pos, 0.0);
        break;
    case REL_TIME_PERCENT:
        if (length != 0)
            return length * (t.pos / 100.0);
        break;
    case REL_TIME_CHAPTER:
        if (chapter_start_time(mpctx, t.pos) >= 0)
            return chapter_start_time(mpctx, t.pos);
        break;
    }
    return fallback_time;
}

static double get_play_end_pts(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    if (opts->play_end.type) {
        return rel_time_to_abs(mpctx, opts->play_end, MP_NOPTS_VALUE);
    } else if (opts->play_length.type) {
        double start = rel_time_to_abs(mpctx, opts->play_start, 0);
        double length = rel_time_to_abs(mpctx, opts->play_length, -1);
        if (start != -1 && length != -1)
            return start + length;
    }
    return MP_NOPTS_VALUE;
}

// Time used to seek external tracks to.
static double get_main_demux_pts(struct MPContext *mpctx)
{
    double main_new_pos = MP_NOPTS_VALUE;
    if (mpctx->demuxer) {
        for (int n = 0; n < mpctx->demuxer->num_streams; n++) {
            if (main_new_pos == MP_NOPTS_VALUE)
                main_new_pos = demux_get_next_pts(mpctx->demuxer->streams[n]);
        }
    }
    return main_new_pos;
}

static int check_framedrop(struct MPContext *mpctx, double frame_time)
{
    struct MPOpts *opts = mpctx->opts;
    // check for frame-drop:
    if (mpctx->sh_audio && !mpctx->ao->untimed &&
        !demux_stream_eof(mpctx->sh_audio->gsh))
    {
        float delay = opts->playback_speed * ao_get_delay(mpctx->ao);
        float d = delay - mpctx->delay;
        if (frame_time < 0)
            frame_time = mpctx->sh_video->fps > 0 ? 1.0 / mpctx->sh_video->fps : 0;
        // we should avoid dropping too many frames in sequence unless we
        // are too late. and we allow 100ms A-V delay here:
        if (d < -mpctx->dropped_frames * frame_time - 0.100 && !mpctx->paused
            && !mpctx->restart_playback) {
            mpctx->drop_frame_cnt++;
            mpctx->dropped_frames++;
            return mpctx->opts->frame_dropping;
        } else
            mpctx->dropped_frames = 0;
    }
    return 0;
}

static double timing_sleep(struct MPContext *mpctx, double time_frame)
{
    // assume kernel HZ=100 for softsleep, works with larger HZ but with
    // unnecessarily high CPU usage
    struct MPOpts *opts = mpctx->opts;
    double margin = opts->softsleep ? 0.011 : 0;
    while (time_frame > margin) {
        mp_sleep_us(1000000 * (time_frame - margin));
        time_frame -= get_relative_time(mpctx);
    }
    if (opts->softsleep) {
        if (time_frame < 0)
            MP_WARN(mpctx, "Warning! Softsleep underflow!\n");
        while (time_frame > 0)
            time_frame -= get_relative_time(mpctx);  // burn the CPU
    }
    return time_frame;
}

void pause_player(struct MPContext *mpctx)
{
    mp_notify_property(mpctx, "pause");

    mpctx->opts->pause = 1;

    if (mpctx->video_out)
        vo_control(mpctx->video_out, VOCTRL_RESTORE_SCREENSAVER, NULL);

    if (mpctx->paused)
        return;
    mpctx->paused = true;
    mpctx->step_frames = 0;
    mpctx->time_frame -= get_relative_time(mpctx);
    mpctx->osd_function = 0;
    mpctx->paused_for_cache = false;

    if (mpctx->video_out && mpctx->sh_video && mpctx->video_out->config_ok)
        vo_control(mpctx->video_out, VOCTRL_PAUSE, NULL);

    if (mpctx->ao && mpctx->sh_audio)
        ao_pause(mpctx->ao);    // pause audio, keep data if possible

    // Only print status if there's actually a file being played.
    if (mpctx->num_sources)
        print_status(mpctx);

    if (!mpctx->opts->quiet)
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_PAUSED\n");
}

void unpause_player(struct MPContext *mpctx)
{
    mp_notify_property(mpctx, "pause");

    mpctx->opts->pause = 0;

    if (mpctx->video_out && mpctx->opts->stop_screensaver)
        vo_control(mpctx->video_out, VOCTRL_KILL_SCREENSAVER, NULL);

    if (!mpctx->paused)
        return;
    // Don't actually unpause while cache is loading.
    if (mpctx->paused_for_cache)
        return;
    mpctx->paused = false;
    mpctx->osd_function = 0;

    if (mpctx->ao && mpctx->sh_audio)
        ao_resume(mpctx->ao);
    if (mpctx->video_out && mpctx->sh_video && mpctx->video_out->config_ok)
        vo_control(mpctx->video_out, VOCTRL_RESUME, NULL);      // resume video
    (void)get_relative_time(mpctx);     // ignore time that passed during pause
}

static void draw_osd(struct MPContext *mpctx)
{
    struct vo *vo = mpctx->video_out;

    mpctx->osd->vo_pts = mpctx->video_pts;
    vo_draw_osd(vo, mpctx->osd);
}

static bool redraw_osd(struct MPContext *mpctx)
{
    struct vo *vo = mpctx->video_out;
    if (vo_redraw_frame(vo) < 0)
        return false;

    draw_osd(mpctx);

    vo_flip_page(vo, 0, -1);
    return true;
}

void add_step_frame(struct MPContext *mpctx, int dir)
{
    if (!mpctx->sh_video)
        return;
    if (dir > 0) {
        mpctx->step_frames += 1;
        unpause_player(mpctx);
    } else if (dir < 0) {
        if (!mpctx->backstep_active && !mpctx->hrseek_active) {
            mpctx->backstep_active = true;
            mpctx->backstep_start_seek_ts = mpctx->vo_pts_history_seek_ts;
            pause_player(mpctx);
        }
    }
}

static void seek_reset(struct MPContext *mpctx, bool reset_ao, bool reset_ac)
{
    if (mpctx->sh_video) {
        resync_video_stream(mpctx->sh_video);
        vo_seek_reset(mpctx->video_out);
        if (mpctx->sh_video->vf_initialized == 1)
            vf_chain_seek_reset(mpctx->sh_video->vfilter);
        mpctx->sh_video->num_buffered_pts = 0;
        mpctx->sh_video->last_pts = MP_NOPTS_VALUE;
        mpctx->sh_video->pts = MP_NOPTS_VALUE;
        mpctx->video_pts = MP_NOPTS_VALUE;
        mpctx->delay = 0;
        mpctx->time_frame = 0;
    }

    if (mpctx->sh_audio && reset_ac) {
        resync_audio_stream(mpctx->sh_audio);
        if (reset_ao)
            ao_reset(mpctx->ao);
        mpctx->ao->buffer.len = mpctx->ao->buffer_playable_size;
        mpctx->sh_audio->a_buffer_len = 0;
    }

    reset_subtitles(mpctx);

    mpctx->restart_playback = true;
    mpctx->hrseek_active = false;
    mpctx->hrseek_framedrop = false;
    mpctx->total_avsync_change = 0;
    mpctx->drop_frame_cnt = 0;
    mpctx->dropped_frames = 0;
    mpctx->playback_pts = MP_NOPTS_VALUE;

#ifdef CONFIG_ENCODING
    encode_lavc_discontinuity(mpctx->encode_lavc_ctx);
#endif
}

static bool timeline_set_part(struct MPContext *mpctx, int i, bool force)
{
    struct timeline_part *p = mpctx->timeline + mpctx->timeline_part;
    struct timeline_part *n = mpctx->timeline + i;
    mpctx->timeline_part = i;
    mpctx->video_offset = n->start - n->source_start;
    if (n->source == p->source && !force)
        return false;
    enum stop_play_reason orig_stop_play = mpctx->stop_play;
    if (!mpctx->sh_video && mpctx->stop_play == KEEP_PLAYING)
        mpctx->stop_play = AT_END_OF_FILE;  // let audio uninit drain data
    uninit_player(mpctx, INITIALIZED_VCODEC | (mpctx->opts->fixed_vo ? 0 : INITIALIZED_VO) | (mpctx->opts->gapless_audio ? 0 : INITIALIZED_AO) | INITIALIZED_ACODEC | INITIALIZED_SUB);
    mpctx->stop_play = orig_stop_play;

    mpctx->demuxer = n->source;
    mpctx->stream = mpctx->demuxer->stream;

    // While another timeline was active, the selection of active tracks might
    // have been changed - possibly we need to update this source.
    for (int x = 0; x < mpctx->num_tracks; x++) {
        struct track *track = mpctx->tracks[x];
        if (track->under_timeline) {
            track->demuxer = mpctx->demuxer;
            track->stream = demuxer_stream_by_demuxer_id(track->demuxer,
                                                         track->type,
                                                         track->demuxer_id);
        }
    }
    preselect_demux_streams(mpctx);

    return true;
}

// Given pts, switch playback to the corresponding part.
// Return offset within that part.
static double timeline_set_from_time(struct MPContext *mpctx, double pts,
                                     bool *need_reset)
{
    if (pts < 0)
        pts = 0;
    for (int i = 0; i < mpctx->num_timeline_parts; i++) {
        struct timeline_part *p = mpctx->timeline + i;
        if (pts < (p + 1)->start) {
            *need_reset = timeline_set_part(mpctx, i, false);
            return pts - p->start + p->source_start;
        }
    }
    return -1;
}


// return -1 if seek failed (non-seekable stream?), 0 otherwise
static int seek(MPContext *mpctx, struct seek_params seek,
                bool timeline_fallthrough)
{
    struct MPOpts *opts = mpctx->opts;
    uint64_t prev_seek_ts = mpctx->vo_pts_history_seek_ts;

    if (!mpctx->demuxer)
        return -1;

    if (mpctx->stop_play == AT_END_OF_FILE)
        mpctx->stop_play = KEEP_PLAYING;
    bool hr_seek = mpctx->demuxer->accurate_seek && opts->correct_pts;
    hr_seek &= seek.exact >= 0 && seek.type != MPSEEK_FACTOR;
    hr_seek &= (opts->hr_seek == 0 && seek.type == MPSEEK_ABSOLUTE) ||
               opts->hr_seek > 0 || seek.exact > 0;
    if (seek.type == MPSEEK_FACTOR || seek.amount < 0 ||
        (seek.type == MPSEEK_ABSOLUTE && seek.amount < mpctx->last_chapter_pts))
        mpctx->last_chapter_seek = -2;
    if (seek.type == MPSEEK_FACTOR) {
        double len = get_time_length(mpctx);
        if (len > 0 && !mpctx->demuxer->ts_resets_possible) {
            seek.amount = seek.amount * len + get_start_time(mpctx);
            seek.type = MPSEEK_ABSOLUTE;
        }
    }
    if ((mpctx->demuxer->accurate_seek || mpctx->timeline)
        && seek.type == MPSEEK_RELATIVE) {
        seek.type = MPSEEK_ABSOLUTE;
        seek.direction = seek.amount > 0 ? 1 : -1;
        seek.amount += get_current_time(mpctx);
    }

    /* At least the liba52 decoder wants to read from the input stream
     * during initialization, so reinit must be done after the demux_seek()
     * call that clears possible stream EOF. */
    bool need_reset = false;
    double demuxer_amount = seek.amount;
    if (mpctx->timeline) {
        demuxer_amount = timeline_set_from_time(mpctx, seek.amount,
                                                &need_reset);
        if (demuxer_amount == -1) {
            assert(!need_reset);
            mpctx->stop_play = AT_END_OF_FILE;
            // Clear audio from current position
            if (mpctx->sh_audio && !timeline_fallthrough) {
                ao_reset(mpctx->ao);
                mpctx->sh_audio->a_buffer_len = 0;
            }
            return -1;
        }
    }
    if (need_reset) {
        reinit_video_chain(mpctx);
        reinit_subs(mpctx);
    }

    int demuxer_style = 0;
    switch (seek.type) {
    case MPSEEK_FACTOR:
        demuxer_style |= SEEK_ABSOLUTE | SEEK_FACTOR;
        break;
    case MPSEEK_ABSOLUTE:
        demuxer_style |= SEEK_ABSOLUTE;
        break;
    }
    if (hr_seek || seek.direction < 0)
        demuxer_style |= SEEK_BACKWARD;
    else if (seek.direction > 0)
        demuxer_style |= SEEK_FORWARD;
    if (hr_seek || opts->mkv_subtitle_preroll)
        demuxer_style |= SEEK_SUBPREROLL;

    if (hr_seek)
        demuxer_amount -= opts->hr_seek_demuxer_offset;
    int seekresult = demux_seek(mpctx->demuxer, demuxer_amount, demuxer_style);
    if (seekresult == 0) {
        if (need_reset) {
            reinit_audio_chain(mpctx);
            seek_reset(mpctx, !timeline_fallthrough, false);
        }
        return -1;
    }

    // If audio or demuxer subs come from different files, seek them too:
    bool have_external_tracks = false;
    for (int type = 0; type < STREAM_TYPE_COUNT; type++) {
        struct track *track = mpctx->current_track[type];
        have_external_tracks |= track && track->is_external && track->demuxer;
    }
    if (have_external_tracks) {
        double main_new_pos;
        if (seek.type == MPSEEK_ABSOLUTE) {
            main_new_pos = seek.amount - mpctx->video_offset;
        } else {
            main_new_pos = get_main_demux_pts(mpctx);
        }
        for (int type = 0; type < STREAM_TYPE_COUNT; type++) {
            struct track *track = mpctx->current_track[type];
            if (track && track->is_external && track->demuxer)
                demux_seek(track->demuxer, main_new_pos, SEEK_ABSOLUTE);
        }
    }

    if (need_reset)
        reinit_audio_chain(mpctx);
    /* If we just reinitialized audio it doesn't need to be reset,
     * and resetting could lose audio some decoders produce during init. */
    seek_reset(mpctx, !timeline_fallthrough, !need_reset);

    if (timeline_fallthrough) {
        // Important if video reinit happens.
        mpctx->vo_pts_history_seek_ts = prev_seek_ts;
    } else {
        mpctx->vo_pts_history_seek_ts++;
        mpctx->backstep_active = false;
    }

    /* Use the target time as "current position" for further relative
     * seeks etc until a new video frame has been decoded */
    if (seek.type == MPSEEK_ABSOLUTE) {
        mpctx->video_pts = seek.amount;
        mpctx->last_seek_pts = seek.amount;
    } else
        mpctx->last_seek_pts = MP_NOPTS_VALUE;

    // The hr_seek==false case is for skipping frames with PTS before the
    // current timeline chapter start. It's not really known where the demuxer
    // level seek will end up, so the hrseek mechanism is abused to skip all
    // frames before chapter start by setting hrseek_pts to the chapter start.
    // It does nothing when the seek is inside of the current chapter, and
    // seeking past the chapter is handled elsewhere.
    if (hr_seek || mpctx->timeline) {
        mpctx->hrseek_active = true;
        mpctx->hrseek_framedrop = true;
        mpctx->hrseek_pts = hr_seek ? seek.amount
                                 : mpctx->timeline[mpctx->timeline_part].start;
    }

    mpctx->start_timestamp = mp_time_sec();

    return 0;
}

void queue_seek(struct MPContext *mpctx, enum seek_type type, double amount,
                int exact)
{
    struct seek_params *seek = &mpctx->seek;
    switch (type) {
    case MPSEEK_RELATIVE:
        if (seek->type == MPSEEK_FACTOR)
            return;  // Well... not common enough to bother doing better
        seek->amount += amount;
        seek->exact = FFMAX(seek->exact, exact);
        if (seek->type == MPSEEK_NONE)
            seek->exact = exact;
        if (seek->type == MPSEEK_ABSOLUTE)
            return;
        if (seek->amount == 0) {
            *seek = (struct seek_params){ 0 };
            return;
        }
        seek->type = MPSEEK_RELATIVE;
        return;
    case MPSEEK_ABSOLUTE:
    case MPSEEK_FACTOR:
        *seek = (struct seek_params) {
            .type = type,
            .amount = amount,
            .exact = exact,
        };
        return;
    case MPSEEK_NONE:
        *seek = (struct seek_params){ 0 };
        return;
    }
    abort();
}

static void execute_queued_seek(struct MPContext *mpctx)
{
    if (mpctx->seek.type) {
        seek(mpctx, mpctx->seek, false);
        mpctx->seek = (struct seek_params){0};
    }
}

// Seek to a given chapter. Tries to queue the seek, but might seek immediately
// in some cases. Returns success, no matter if seek is queued or immediate.
bool mp_seek_chapter(struct MPContext *mpctx, int chapter)
{
    int num = get_chapter_count(mpctx);
    if (num == 0)
        return false;
    if (chapter < -1 || chapter >= num)
        return false;

    mpctx->last_chapter_seek = -2;

    double pts;
    if (chapter == -1) {
        pts = get_start_time(mpctx);
        goto do_seek;
    } else if (mpctx->chapters) {
        pts = mpctx->chapters[chapter].start;
        goto do_seek;
    } else if (mpctx->master_demuxer) {
        int res = demuxer_seek_chapter(mpctx->master_demuxer, chapter, &pts);
        if (res >= 0) {
            if (pts == -1) {
                // for DVD/BD - seek happened via stream layer
                seek_reset(mpctx, true, true);
                mpctx->seek = (struct seek_params){0};
                return true;
            }
            chapter = res;
            goto do_seek;
        }
    }
    return false;

do_seek:
    queue_seek(mpctx, MPSEEK_ABSOLUTE, pts, 0);
    mpctx->last_chapter_seek = chapter;
    mpctx->last_chapter_pts = pts;
    return true;
}

static void update_avsync(struct MPContext *mpctx)
{
    if (!mpctx->sh_audio || !mpctx->sh_video)
        return;

    double a_pos = playing_audio_pts(mpctx);

    mpctx->last_av_difference = a_pos - mpctx->video_pts - mpctx->audio_delay;
    if (mpctx->time_frame > 0)
        mpctx->last_av_difference +=
                mpctx->time_frame * mpctx->opts->playback_speed;
    if (a_pos == MP_NOPTS_VALUE || mpctx->video_pts == MP_NOPTS_VALUE)
        mpctx->last_av_difference = MP_NOPTS_VALUE;
    if (mpctx->last_av_difference > 0.5 && mpctx->drop_frame_cnt > 50
        && !mpctx->drop_message_shown) {
        MP_WARN(mpctx, "%s", mp_gtext(av_desync_help_text));
        mpctx->drop_message_shown = true;
    }
}

static bool handle_osd_redraw(struct MPContext *mpctx)
{
    if (!mpctx->video_out || !mpctx->video_out->config_ok)
        return false;
    bool want_redraw = vo_get_want_redraw(mpctx->video_out);
    if (mpctx->video_out->driver->draw_osd)
        want_redraw |= mpctx->osd->want_redraw;
    mpctx->osd->want_redraw = false;
    if (want_redraw) {
        if (redraw_osd(mpctx))
            return true;
    }
    return false;
}

static void handle_metadata_update(struct MPContext *mpctx)
{
    if (mp_time_sec() > mpctx->last_metadata_update + 2) {
        demux_info_update(mpctx->demuxer);
        mpctx->last_metadata_update = mp_time_sec();
    }
}

static void handle_pause_on_low_cache(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    int cache = mp_get_cache_percent(mpctx);
    bool idle = mp_get_cache_idle(mpctx);
    if (mpctx->paused && mpctx->paused_for_cache) {
        if (cache < 0 || cache >= opts->stream_cache_min_percent || idle) {
            mpctx->paused_for_cache = false;
            if (!opts->pause)
                unpause_player(mpctx);
        }
    } else {
        if (cache >= 0 && cache <= opts->stream_cache_pause && !idle) {
            bool prev_paused_user = opts->pause;
            pause_player(mpctx);
            mpctx->paused_for_cache = true;
            opts->pause = prev_paused_user;
        }
    }
}

static void handle_heartbeat_cmd(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    if (opts->heartbeat_cmd && !mpctx->paused) {
        double now = mp_time_sec();
        if (now - mpctx->last_heartbeat > opts->heartbeat_interval) {
            mpctx->last_heartbeat = now;
            system(opts->heartbeat_cmd);
        }
    }
}

static void handle_cursor_autohide(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct vo *vo = mpctx->video_out;

    if (!vo)
        return;

    bool mouse_cursor_visible = mpctx->mouse_cursor_visible;

    unsigned mouse_event_ts = mp_input_get_mouse_event_counter(mpctx->input);
    if (mpctx->mouse_event_ts != mouse_event_ts) {
        mpctx->mouse_event_ts = mouse_event_ts;
        mpctx->mouse_timer =
            mp_time_sec() + opts->cursor_autohide_delay / 1000.0;
        mouse_cursor_visible = true;
    }

    if (mp_time_sec() >= mpctx->mouse_timer)
        mouse_cursor_visible = false;

    if (opts->cursor_autohide_delay == -1)
        mouse_cursor_visible = true;

    if (opts->cursor_autohide_delay == -2)
        mouse_cursor_visible = false;

    if (opts->cursor_autohide_fs && !opts->vo.fullscreen)
        mouse_cursor_visible = true;

    if (mouse_cursor_visible != mpctx->mouse_cursor_visible)
        vo_control(vo, VOCTRL_SET_CURSOR_VISIBILITY, &mouse_cursor_visible);
    mpctx->mouse_cursor_visible = mouse_cursor_visible;
}

static void handle_input_and_seek_coalesce(struct MPContext *mpctx)
{
    mp_flush_events(mpctx);

    mp_cmd_t *cmd;
    while ((cmd = mp_input_get_cmd(mpctx->input, 0, 1)) != NULL) {
        /* Allow running consecutive seek commands to combine them,
         * but execute the seek before running other commands.
         * If the user seeks continuously (keeps arrow key down)
         * try to finish showing a frame from one location before doing
         * another seek (which could lead to unchanging display). */
        if ((mpctx->seek.type && cmd->id != MP_CMD_SEEK) ||
            (mpctx->restart_playback && cmd->id == MP_CMD_SEEK &&
             mp_time_sec() - mpctx->start_timestamp < 0.3))
            break;
        cmd = mp_input_get_cmd(mpctx->input, 0, 0);
        run_command(mpctx, cmd);
        mp_cmd_free(cmd);
        if (mpctx->stop_play)
            break;
    }
}

static void handle_backstep(struct MPContext *mpctx)
{
    if (!mpctx->backstep_active)
        return;

    double current_pts = mpctx->last_vo_pts;
    mpctx->backstep_active = false;
    bool demuxer_ok = mpctx->demuxer && mpctx->demuxer->accurate_seek;
    if (demuxer_ok && mpctx->sh_video && current_pts != MP_NOPTS_VALUE) {
        double seek_pts = find_previous_pts(mpctx, current_pts);
        if (seek_pts != MP_NOPTS_VALUE) {
            queue_seek(mpctx, MPSEEK_ABSOLUTE, seek_pts, 1);
        } else {
            double last = get_last_frame_pts(mpctx);
            if (last != MP_NOPTS_VALUE && last >= current_pts &&
                mpctx->backstep_start_seek_ts != mpctx->vo_pts_history_seek_ts)
            {
                MP_ERR(mpctx, "Backstep failed.\n");
                queue_seek(mpctx, MPSEEK_ABSOLUTE, current_pts, 1);
            } else if (!mpctx->hrseek_active) {
                MP_VERBOSE(mpctx, "Start backstep indexing.\n");
                // Force it to index the video up until current_pts.
                // The whole point is getting frames _before_ that PTS,
                // so apply an arbitrary offset. (In theory the offset
                // has to be large enough to reach the previous frame.)
                seek(mpctx, (struct seek_params){
                            .type = MPSEEK_ABSOLUTE,
                            .amount = current_pts - 1.0,
                            }, false);
                // Don't leave hr-seek mode. If all goes right, hr-seek
                // mode is cancelled as soon as the frame before
                // current_pts is found during hr-seeking.
                // Note that current_pts should be part of the index,
                // otherwise we can't find the previous frame, so set the
                // seek target an arbitrary amount of time after it.
                if (mpctx->hrseek_active) {
                    mpctx->hrseek_pts = current_pts + 10.0;
                    mpctx->hrseek_framedrop = false;
                    mpctx->backstep_active = true;
                }
            } else {
                mpctx->backstep_active = true;
            }
        }
    }
}

static void handle_sstep(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    if (opts->step_sec > 0 && !mpctx->stop_play && !mpctx->paused &&
        !mpctx->restart_playback)
    {
        set_osd_function(mpctx, OSD_FFW);
        queue_seek(mpctx, MPSEEK_RELATIVE, opts->step_sec, 0);
    }
}

static void handle_keep_open(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    if (opts->keep_open && mpctx->stop_play == AT_END_OF_FILE) {
        mpctx->stop_play = KEEP_PLAYING;
        mpctx->playback_pts = mpctx->last_vo_pts;
        pause_player(mpctx);
    }
}

// Execute a forceful refresh of the VO window, if it hasn't had a valid frame
// for a while. The problem is that a VO with no valid frame (vo->hasframe==0)
// doesn't redraw video and doesn't OSD interaction. So screw it, hard.
static void handle_force_window(struct MPContext *mpctx, bool reconfig)
{
    // Don't interfere with real video playback
    if (mpctx->sh_video)
        return;

    struct vo *vo = mpctx->video_out;
    if (!vo)
        return;

    if (!vo->config_ok || reconfig) {
        MP_INFO(mpctx, "Creating non-video VO window.\n");
        // Pick whatever works
        int config_format = 0;
        for (int fmt = IMGFMT_START; fmt < IMGFMT_END; fmt++) {
            if (vo->driver->query_format(vo, fmt)) {
                config_format = fmt;
                break;
            }
        }
        int w = 960;
        int h = 480;
        struct mp_image_params p = {
            .imgfmt = config_format,
            .w = w,   .h = h,
            .d_w = w, .d_h = h,
        };
        vo_reconfig(vo, &p, 0);
        redraw_osd(mpctx);
    }
}

static double get_wakeup_period(struct MPContext *mpctx)
{
    /* Even if we can immediately wake up in response to most input events,
     * there are some timers which are not registered to the event loop
     * and need to be checked periodically (like automatic mouse cursor hiding).
     * OSD content updates behave similarly. Also some uncommon input devices
     * may not have proper FD event support.
     */
    double sleeptime = WAKEUP_PERIOD;

#ifndef HAVE_POSIX_SELECT
    // No proper file descriptor event handling; keep waking up to poll input
    sleeptime = FFMIN(sleeptime, 0.02);
#endif

    if (mpctx->video_out)
        if (mpctx->video_out->wakeup_period > 0)
            sleeptime = FFMIN(sleeptime, mpctx->video_out->wakeup_period);

    return sleeptime;
}

static void run_playloop(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    bool full_audio_buffers = false;
    bool audio_left = false, video_left = false;
    double endpts = get_play_end_pts(mpctx);
    bool end_is_chapter = false;
    double sleeptime = get_wakeup_period(mpctx);
    bool was_restart = mpctx->restart_playback;
    bool new_frame_shown = false;

#ifdef CONFIG_ENCODING
    if (encode_lavc_didfail(mpctx->encode_lavc_ctx)) {
        mpctx->stop_play = PT_QUIT;
        return;
    }
#endif

    // Add tracks that were added by the demuxer later (e.g. MPEG)
    if (!mpctx->timeline && mpctx->demuxer)
        add_demuxer_tracks(mpctx, mpctx->demuxer);

    if (mpctx->timeline) {
        double end = mpctx->timeline[mpctx->timeline_part + 1].start;
        if (endpts == MP_NOPTS_VALUE || end < endpts) {
            endpts = end;
            end_is_chapter = true;
        }
    }

    if (opts->chapterrange[1] > 0) {
        int cur_chapter = get_current_chapter(mpctx);
        if (cur_chapter != -1 && cur_chapter + 1 > opts->chapterrange[1])
            mpctx->stop_play = PT_NEXT_ENTRY;
    }

    if (mpctx->sh_audio && !mpctx->restart_playback && !mpctx->ao->untimed) {
        int status = fill_audio_out_buffers(mpctx, endpts);
        full_audio_buffers = status >= 0;
        // Not at audio stream EOF yet
        audio_left = status > -2;
    }

    if (mpctx->video_out) {
        vo_check_events(mpctx->video_out);
        handle_cursor_autohide(mpctx);
    }

    double buffered_audio = -1;
    while (mpctx->sh_video) {   // never loops, for "break;" only
        struct vo *vo = mpctx->video_out;
        update_fps(mpctx);

        video_left = vo->hasframe || vo->frame_loaded;
        if (!vo->frame_loaded && (!mpctx->paused || mpctx->restart_playback)) {
            double frame_time = update_video(mpctx, endpts);
            mp_dbg(MSGT_AVSYNC, MSGL_DBG2, "*** ftime=%5.3f ***\n", frame_time);
            if (mpctx->sh_video->vf_initialized < 0) {
                MP_FATAL(mpctx, "\nFATAL: Could not initialize video filters "
                         "(-vf) or video output (-vo).\n");
                int uninit = INITIALIZED_VCODEC;
                if (!opts->force_vo)
                    uninit |= INITIALIZED_VO;
                uninit_player(mpctx, uninit);
                mpctx->current_track[STREAM_VIDEO] = NULL;
                if (!mpctx->current_track[STREAM_AUDIO])
                    mpctx->stop_play = PT_NEXT_ENTRY;
                mpctx->error_playing = true;
                handle_force_window(mpctx, true);
                break;
            }
            video_left = frame_time >= 0;
            if (video_left && !mpctx->restart_playback) {
                mpctx->time_frame += frame_time / opts->playback_speed;
                adjust_sync(mpctx, frame_time);
            }
            if (!video_left) {
                mpctx->delay = 0;
                mpctx->last_av_difference = 0;
            }
        }

        if (endpts != MP_NOPTS_VALUE)
            video_left &= mpctx->sh_video->pts < endpts;

        handle_heartbeat_cmd(mpctx);

        if (!video_left || (mpctx->paused && !mpctx->restart_playback))
            break;
        if (!vo->frame_loaded) {
            sleeptime = 0;
            break;
        }

        mpctx->time_frame -= get_relative_time(mpctx);
        if (full_audio_buffers && !mpctx->restart_playback) {
            buffered_audio = ao_get_delay(mpctx->ao);
            mp_dbg(MSGT_AVSYNC, MSGL_DBG2, "delay=%f\n", buffered_audio);

            if (opts->autosync) {
                /* Smooth reported playback position from AO by averaging
                 * it with the value expected based on previus value and
                 * time elapsed since then. May help smooth video timing
                 * with audio output that have inaccurate position reporting.
                 * This is badly implemented; the behavior of the smoothing
                 * now undesirably depends on how often this code runs
                 * (mainly depends on video frame rate). */
                float predicted = (mpctx->delay / opts->playback_speed +
                                   mpctx->time_frame);
                float difference = buffered_audio - predicted;
                buffered_audio = predicted + difference / opts->autosync;
            }

            mpctx->time_frame = (buffered_audio -
                                 mpctx->delay / opts->playback_speed);
        } else {
            /* If we're more than 200 ms behind the right playback
             * position, don't try to speed up display of following
             * frames to catch up; continue with default speed from
             * the current frame instead.
             * If untimed is set always output frames immediately
             * without sleeping.
             */
            if (mpctx->time_frame < -0.2 || opts->untimed || vo->untimed)
                mpctx->time_frame = 0;
        }

        double vsleep = mpctx->time_frame - vo->flip_queue_offset;
        if (vsleep > 0.050) {
            sleeptime = FFMIN(sleeptime, vsleep - 0.040);
            break;
        }
        sleeptime = 0;

        //=================== FLIP PAGE (VIDEO BLT): ======================

        vo_new_frame_imminent(vo);
        struct sh_video *sh_video = mpctx->sh_video;
        mpctx->video_pts = sh_video->pts;
        mpctx->last_vo_pts = mpctx->video_pts;
        mpctx->playback_pts = mpctx->video_pts;
        update_subtitles(mpctx);
        update_osd_msg(mpctx);
        draw_osd(mpctx);

        mpctx->time_frame -= get_relative_time(mpctx);
        mpctx->time_frame -= vo->flip_queue_offset;
        if (mpctx->time_frame > 0.001)
            mpctx->time_frame = timing_sleep(mpctx, mpctx->time_frame);
        mpctx->time_frame += vo->flip_queue_offset;

        int64_t t2 = mp_time_us();
        /* Playing with playback speed it's possible to get pathological
         * cases with mpctx->time_frame negative enough to cause an
         * overflow in pts_us calculation, thus the FFMAX. */
        double time_frame = FFMAX(mpctx->time_frame, -1);
        int64_t pts_us = mpctx->last_time + time_frame * 1e6;
        int duration = -1;
        double pts2 = vo->next_pts2;
        if (pts2 != MP_NOPTS_VALUE && opts->correct_pts &&
                !mpctx->restart_playback) {
            // expected A/V sync correction is ignored
            double diff = (pts2 - mpctx->video_pts);
            diff /= opts->playback_speed;
            if (mpctx->time_frame < 0)
                diff += mpctx->time_frame;
            if (diff < 0)
                diff = 0;
            if (diff > 10)
                diff = 10;
            duration = diff * 1e6;
        }
        vo_flip_page(vo, pts_us | 1, duration);

        mpctx->last_vo_flip_duration = (mp_time_us() - t2) * 0.000001;
        if (vo->driver->flip_page_timed) {
            // No need to adjust sync based on flip speed
            mpctx->last_vo_flip_duration = 0;
            // For print_status - VO call finishing early is OK for sync
            mpctx->time_frame -= get_relative_time(mpctx);
        }
        mpctx->shown_vframes++;
        if (mpctx->restart_playback) {
            if (mpctx->sync_audio_to_video) {
                mpctx->syncing_audio = true;
                if (mpctx->sh_audio)
                    fill_audio_out_buffers(mpctx, endpts);
                mpctx->restart_playback = false;
            }
            mpctx->time_frame = 0;
            get_relative_time(mpctx);
        }
        update_avsync(mpctx);
        print_status(mpctx);
        screenshot_flip(mpctx);
        new_frame_shown = true;

        break;
    } // video

    video_left &= mpctx->sync_audio_to_video; // force no-video semantics

    if (mpctx->sh_audio && (mpctx->restart_playback ? !video_left :
                            mpctx->ao->untimed && (mpctx->delay <= 0 ||
                                                   !video_left))) {
        int status = fill_audio_out_buffers(mpctx, endpts);
        full_audio_buffers = status >= 0 && !mpctx->ao->untimed;
        // Not at audio stream EOF yet
        audio_left = status > -2;
    }
    if (!video_left)
        mpctx->restart_playback = false;
    if (mpctx->sh_audio && buffered_audio == -1)
        buffered_audio = mpctx->paused ? 0 : ao_get_delay(mpctx->ao);

    update_osd_msg(mpctx);

    // The cache status is part of the status line. Possibly update it.
    if (mpctx->paused && mp_get_cache_percent(mpctx) >= 0)
        print_status(mpctx);

    if (!video_left && (!mpctx->paused || was_restart)) {
        double a_pos = 0;
        if (mpctx->sh_audio) {
            a_pos = (written_audio_pts(mpctx) -
                     mpctx->opts->playback_speed * buffered_audio);
        }
        mpctx->playback_pts = a_pos;
        print_status(mpctx);
    }

    update_subtitles(mpctx);

    /* It's possible for the user to simultaneously switch both audio
     * and video streams to "disabled" at runtime. Handle this by waiting
     * rather than immediately stopping playback due to EOF.
     *
     * When all audio has been written to output driver, stay in the
     * main loop handling commands until it has been mostly consumed,
     * except in the gapless case, where the next file will be started
     * while audio from the current one still remains to be played.
     *
     * We want this check to trigger if we seeked to this position,
     * but not if we paused at it with audio possibly still buffered in
     * the AO. There's currently no working way to check buffered audio
     * inside AO while paused. Thus the "was_restart" check below, which
     * should trigger after seek only, when we know there's no audio
     * buffered.
     */
    if ((mpctx->sh_audio || mpctx->sh_video) && !audio_left && !video_left
        && (opts->gapless_audio || buffered_audio < 0.05)
        && (!mpctx->paused || was_restart)) {
        if (end_is_chapter) {
            seek(mpctx, (struct seek_params){
                        .type = MPSEEK_ABSOLUTE,
                        .amount = mpctx->timeline[mpctx->timeline_part+1].start
                        }, true);
        } else
            mpctx->stop_play = AT_END_OF_FILE;
        sleeptime = 0;
    }

    if (!mpctx->stop_play && !mpctx->restart_playback) {

        // If no more video is available, one frame means one playloop iteration.
        // Otherwise, one frame means one video frame.
        if (!video_left)
            new_frame_shown = true;

        if (opts->playing_msg && !mpctx->playing_msg_shown && new_frame_shown) {
            mpctx->playing_msg_shown = true;
            char *msg = mp_property_expand_string(mpctx, opts->playing_msg);
            MP_INFO(mpctx, "%s\n", msg);
            talloc_free(msg);
        }

        if (mpctx->max_frames >= 0) {
            if (new_frame_shown)
                mpctx->max_frames--;
            if (mpctx->max_frames <= 0)
                mpctx->stop_play = PT_NEXT_ENTRY;
        }

        if (mpctx->step_frames > 0 && !mpctx->paused) {
            if (new_frame_shown)
                mpctx->step_frames--;
            if (mpctx->step_frames == 0)
                pause_player(mpctx);
        }

    }

    if (!mpctx->stop_play) {
        double audio_sleep = 9;
        if (mpctx->sh_audio && !mpctx->paused) {
            if (mpctx->ao->untimed) {
                if (!video_left)
                    audio_sleep = 0;
            } else if (full_audio_buffers) {
                audio_sleep = buffered_audio - 0.050;
                // Keep extra safety margin if the buffers are large
                if (audio_sleep > 0.100)
                    audio_sleep = FFMAX(audio_sleep - 0.200, 0.100);
                else
                    audio_sleep = FFMAX(audio_sleep, 0.020);
            } else
                audio_sleep = 0.020;
        }
        sleeptime = FFMIN(sleeptime, audio_sleep);
        if (sleeptime > 0) {
            if (handle_osd_redraw(mpctx))
                sleeptime = 0;
        }
        if (sleeptime > 0)
            mp_input_get_cmd(mpctx->input, sleeptime * 1000, true);
    }

    handle_metadata_update(mpctx);

    handle_pause_on_low_cache(mpctx);

    handle_input_and_seek_coalesce(mpctx);

    handle_backstep(mpctx);

    handle_sstep(mpctx);

    handle_keep_open(mpctx);

    handle_force_window(mpctx, false);

    execute_queued_seek(mpctx);
}
