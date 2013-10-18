
// Waiting for the slave master to send us a new file to play.
static void idle_loop(struct MPContext *mpctx)
{
    // ================= idle loop (STOP state) =========================
    bool need_reinit = true;
    while (mpctx->opts->player_idle_mode && !mpctx->playlist->current
           && mpctx->stop_play != PT_QUIT)
    {
        if (need_reinit)
            handle_force_window(mpctx, true);
        need_reinit = false;
        int uninit = INITIALIZED_AO;
        if (!mpctx->opts->force_vo)
            uninit |= INITIALIZED_VO;
        uninit_player(mpctx, uninit);
        handle_force_window(mpctx, false);
        if (mpctx->video_out)
            vo_check_events(mpctx->video_out);
        update_osd_msg(mpctx);
        handle_osd_redraw(mpctx);
        mp_cmd_t *cmd = mp_input_get_cmd(mpctx->input,
                                         get_wakeup_period(mpctx) * 1000,
                                         false);
        if (cmd)
            run_command(mpctx, cmd);
        mp_cmd_free(cmd);
        mp_flush_events(mpctx);
    }
}

static void stream_dump(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    char *filename = opts->stream_dump;
    stream_t *stream = mpctx->stream;
    assert(stream && filename);

    stream_set_capture_file(stream, filename);

    while (mpctx->stop_play == KEEP_PLAYING && !stream->eof) {
        if (!opts->quiet && ((stream->pos / (1024 * 1024)) % 2) == 1) {
            uint64_t pos = stream->pos - stream->start_pos;
            uint64_t end = stream->end_pos - stream->start_pos;
            char *line = talloc_asprintf(NULL, "Dumping %lld/%lld...",
                (long long int)pos, (long long int)end);
            write_status_line(mpctx, line);
            talloc_free(line);
        }
        stream_fill_buffer(stream);
        for (;;) {
            mp_cmd_t *cmd = mp_input_get_cmd(mpctx->input, 0, false);
            if (!cmd)
                break;
            run_command(mpctx, cmd);
            talloc_free(cmd);
        }
    }
}
