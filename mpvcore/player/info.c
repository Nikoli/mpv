
static void print_stream(struct MPContext *mpctx, struct track *t)
{
    struct sh_stream *s = t->stream;
    const char *tname = "?";
    const char *selopt = "?";
    const char *langopt = "?";
    const char *iid = NULL;
    switch (t->type) {
    case STREAM_VIDEO:
        tname = "Video"; selopt = "vid"; langopt = NULL; iid = "VID";
        break;
    case STREAM_AUDIO:
        tname = "Audio"; selopt = "aid"; langopt = "alang"; iid = "AID";
        break;
    case STREAM_SUB:
        tname = "Subs"; selopt = "sid"; langopt = "slang"; iid = "SID";
        break;
    }
    MP_INFO(mpctx, "[stream] %-5s %3s",
            tname, mpctx->current_track[t->type] == t ? "(+)" : "");
    MP_INFO(mpctx, " --%s=%d", selopt, t->user_tid);
    if (t->lang && langopt)
        MP_INFO(mpctx, " --%s=%s", langopt, t->lang);
    if (t->default_track)
        MP_INFO(mpctx, " (*)");
    if (t->attached_picture)
        MP_INFO(mpctx, " [P]");
    if (t->title)
        MP_INFO(mpctx, " '%s'", t->title);
    const char *codec = s ? s->codec : NULL;
    MP_INFO(mpctx, " (%s)", codec ? codec : "<unknown>");
    if (t->is_external)
        MP_INFO(mpctx, " (external)");
    MP_INFO(mpctx, "\n");
    // legacy compatibility
    if (!iid)
        return;
    int id = t->user_tid;
    mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_%s_ID=%d\n", iid, id);
    if (t->title)
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_%s_%d_NAME=%s\n", iid, id, t->title);
    if (t->lang)
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_%s_%d_LANG=%s\n", iid, id, t->lang);
}

static void print_file_properties(struct MPContext *mpctx, const char *filename)
{
    mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_FILENAME=%s\n",
           filename);
    if (mpctx->sh_video) {
        /* Assume FOURCC if all bytes >= 0x20 (' ') */
        if (mpctx->sh_video->format >= 0x20202020)
            mp_msg(MSGT_IDENTIFY, MSGL_INFO,
                   "ID_VIDEO_FORMAT=%.4s\n", (char *)&mpctx->sh_video->format);
        else
            mp_msg(MSGT_IDENTIFY, MSGL_INFO,
                   "ID_VIDEO_FORMAT=0x%08X\n", mpctx->sh_video->format);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_VIDEO_BITRATE=%d\n", mpctx->sh_video->i_bps * 8);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_VIDEO_WIDTH=%d\n", mpctx->sh_video->disp_w);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_VIDEO_HEIGHT=%d\n", mpctx->sh_video->disp_h);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_VIDEO_FPS=%5.3f\n", mpctx->sh_video->fps);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_VIDEO_ASPECT=%1.4f\n", mpctx->sh_video->aspect);
    }
    if (mpctx->sh_audio) {
        /* Assume FOURCC if all bytes >= 0x20 (' ') */
        if (mpctx->sh_audio->format >= 0x20202020)
            mp_msg(MSGT_IDENTIFY, MSGL_INFO,
                   "ID_AUDIO_FORMAT=%.4s\n", (char *)&mpctx->sh_audio->format);
        else
            mp_msg(MSGT_IDENTIFY, MSGL_INFO,
                   "ID_AUDIO_FORMAT=%d\n", mpctx->sh_audio->format);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_AUDIO_BITRATE=%d\n", mpctx->sh_audio->i_bps * 8);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_AUDIO_RATE=%d\n", mpctx->sh_audio->samplerate);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO,
               "ID_AUDIO_NCH=%d\n", mpctx->sh_audio->channels.num);
    }
    mp_msg(MSGT_IDENTIFY, MSGL_INFO,
           "ID_LENGTH=%.2f\n", get_time_length(mpctx));
    int chapter_count = get_chapter_count(mpctx);
    if (chapter_count >= 0) {
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_CHAPTERS=%d\n", chapter_count);
        for (int i = 0; i < chapter_count; i++) {
            mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_CHAPTER_ID=%d\n", i);
            // print in milliseconds
            double time = chapter_start_time(mpctx, i) * 1000.0;
            mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_CHAPTER_%d_START=%"PRId64"\n",
                   i, (int64_t)(time < 0 ? -1 : time));
            char *name = chapter_name(mpctx, i);
            if (name) {
                mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_CHAPTER_%d_NAME=%s\n", i,
                       name);
                talloc_free(name);
            }
        }
    }
    struct demuxer *demuxer = mpctx->master_demuxer;
    if (demuxer->num_editions > 1)
        MP_INFO(mpctx, "Playing edition %d of %d (--edition=%d).\n",
                demuxer->edition + 1, demuxer->num_editions, demuxer->edition);
    for (int t = 0; t < STREAM_TYPE_COUNT; t++) {
        for (int n = 0; n < mpctx->num_tracks; n++)
            if (mpctx->tracks[n]->type == t)
                print_stream(mpctx, mpctx->tracks[n]);
    }
}


static void vo_update_window_title(struct MPContext *mpctx)
{
    if (!mpctx->video_out)
        return;
    char *title = mp_property_expand_string(mpctx, mpctx->opts->wintitle);
    if (!mpctx->video_out->window_title ||
        strcmp(title, mpctx->video_out->window_title))
    {
        talloc_free(mpctx->video_out->window_title);
        mpctx->video_out->window_title = talloc_steal(mpctx, title);
        vo_control(mpctx->video_out, VOCTRL_UPDATE_WINDOW_TITLE, title);
    } else {
        talloc_free(title);
    }
}

#define saddf(var, ...) (*(var) = talloc_asprintf_append((*var), __VA_ARGS__))

// append time in the hh:mm:ss format (plus fractions if wanted)
static void sadd_hhmmssff(char **buf, double time, bool fractions)
{
    char *s = mp_format_time(time, fractions);
    *buf = talloc_strdup_append(*buf, s);
    talloc_free(s);
}

static void sadd_percentage(char **buf, int percent) {
    if (percent >= 0)
        *buf = talloc_asprintf_append(*buf, " (%d%%)", percent);
}

static int get_term_width(void)
{
    get_screen_size();
    int width = screen_width > 0 ? screen_width : 80;
#if defined(__MINGW32__) || defined(__CYGWIN__)
    /* Windows command line is broken (MinGW's rxvt works, but we
     * should not depend on that). */
    width--;
#endif
    return width;
}

static void write_status_line(struct MPContext *mpctx, const char *line)
{
    struct MPOpts *opts = mpctx->opts;
    if (opts->slave_mode) {
        mp_msg(MSGT_STATUSLINE, MSGL_STATUS, "%s\n", line);
    } else if (erase_to_end_of_line) {
        mp_msg(MSGT_STATUSLINE, MSGL_STATUS,
               "%s%s\r", line, erase_to_end_of_line);
    } else {
        int pos = strlen(line);
        int width = get_term_width() - pos;
        mp_msg(MSGT_STATUSLINE, MSGL_STATUS, "%s%*s\r", line, width, "");
    }
}

static void print_status(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    sh_video_t * const sh_video = mpctx->sh_video;

    vo_update_window_title(mpctx);

    if (opts->quiet)
        return;

    if (opts->status_msg) {
        char *r = mp_property_expand_string(mpctx, opts->status_msg);
        write_status_line(mpctx, r);
        talloc_free(r);
        return;
    }

    char *line = NULL;

    // Playback status
    if (mpctx->paused_for_cache && !opts->pause) {
        saddf(&line, "(Buffering) ");
    } else if (mpctx->paused) {
        saddf(&line, "(Paused) ");
    }

    if (mpctx->sh_audio)
        saddf(&line, "A");
    if (mpctx->sh_video)
        saddf(&line, "V");
    saddf(&line, ": ");

    // Playback position
    double cur = get_current_time(mpctx);
    sadd_hhmmssff(&line, cur, mpctx->opts->osd_fractions);

    double len = get_time_length(mpctx);
    if (len >= 0) {
        saddf(&line, " / ");
        sadd_hhmmssff(&line, len, mpctx->opts->osd_fractions);
    }

    sadd_percentage(&line, get_percent_pos(mpctx));

    // other
    if (opts->playback_speed != 1)
        saddf(&line, " x%4.2f", opts->playback_speed);

    // A-V sync
    if (mpctx->sh_audio && sh_video && mpctx->sync_audio_to_video) {
        if (mpctx->last_av_difference != MP_NOPTS_VALUE)
            saddf(&line, " A-V:%7.3f", mpctx->last_av_difference);
        else
            saddf(&line, " A-V: ???");
        if (fabs(mpctx->total_avsync_change) > 0.05)
            saddf(&line, " ct:%7.3f", mpctx->total_avsync_change);
    }

#ifdef CONFIG_ENCODING
    double position = get_current_pos_ratio(mpctx, true);
    char lavcbuf[80];
    if (encode_lavc_getstatus(mpctx->encode_lavc_ctx, lavcbuf, sizeof(lavcbuf),
            position) >= 0)
    {
        // encoding stats
        saddf(&line, " %s", lavcbuf);
    } else
#endif
    {
        // VO stats
        if (sh_video && mpctx->drop_frame_cnt)
            saddf(&line, " Late: %d", mpctx->drop_frame_cnt);
    }

    int cache = mp_get_cache_percent(mpctx);
    if (cache >= 0)
        saddf(&line, " Cache: %d%%", cache);

    // end
    write_status_line(mpctx, line);
    talloc_free(line);
}


// sym == mpctx->osd_function
static void saddf_osd_function_sym(char **buffer, int sym)
{
    char temp[10];
    osd_get_function_sym(temp, sizeof(temp), sym);
    saddf(buffer, "%s ", temp);
}

static void sadd_osd_status(char **buffer, struct MPContext *mpctx, bool full)
{
    bool fractions = mpctx->opts->osd_fractions;
    int sym = mpctx->osd_function;
    if (!sym) {
        if (mpctx->paused_for_cache && !mpctx->opts->pause) {
            sym = OSD_CLOCK;
        } else if (mpctx->paused || mpctx->step_frames) {
            sym = OSD_PAUSE;
        } else {
            sym = OSD_PLAY;
        }
    }
    saddf_osd_function_sym(buffer, sym);
    char *custom_msg = mpctx->opts->osd_status_msg;
    if (custom_msg && full) {
        char *text = mp_property_expand_string(mpctx, custom_msg);
        *buffer = talloc_strdup_append(*buffer, text);
        talloc_free(text);
    } else {
        sadd_hhmmssff(buffer, get_current_time(mpctx), fractions);
        if (full) {
            saddf(buffer, " / ");
            sadd_hhmmssff(buffer, get_time_length(mpctx), fractions);
            sadd_percentage(buffer, get_percent_pos(mpctx));
            int cache = mp_get_cache_percent(mpctx);
            if (cache >= 0)
                saddf(buffer, " Cache: %d%%", cache);
        }
    }
}

// OSD messages initated by seeking commands are added lazily with this
// function, because multiple successive seek commands can be coalesced.
static void add_seek_osd_messages(struct MPContext *mpctx)
{
    if (mpctx->add_osd_seek_info & OSD_SEEK_INFO_BAR) {
        set_osd_bar(mpctx, OSD_BAR_SEEK, "Position", 0, 1,
                    av_clipf(get_current_pos_ratio(mpctx, false), 0, 1));
        set_osd_bar_chapters(mpctx, OSD_BAR_SEEK);
    }
    if (mpctx->add_osd_seek_info & OSD_SEEK_INFO_TEXT) {
        mp_osd_msg_t *msg = add_osd_msg(mpctx, OSD_MSG_TEXT, 1,
                                        mpctx->opts->osd_duration);
        msg->show_position = true;
    }
    if (mpctx->add_osd_seek_info & OSD_SEEK_INFO_CHAPTER_TEXT) {
        char *chapter = chapter_display_name(mpctx, get_current_chapter(mpctx));
        set_osd_tmsg(mpctx, OSD_MSG_TEXT, 1, mpctx->opts->osd_duration,
                     "Chapter: %s", chapter);
        talloc_free(chapter);
    }
    if ((mpctx->add_osd_seek_info & OSD_SEEK_INFO_EDITION)
        && mpctx->master_demuxer)
    {
        set_osd_tmsg(mpctx, OSD_MSG_TEXT, 1, mpctx->opts->osd_duration,
                     "Playing edition %d of %d.",
                     mpctx->master_demuxer->edition + 1,
                     mpctx->master_demuxer->num_editions);
    }
    mpctx->add_osd_seek_info = 0;
}


double get_time_length(struct MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->demuxer;
    if (!demuxer)
        return 0;

    if (mpctx->timeline)
        return mpctx->timeline[mpctx->num_timeline_parts].start;

    double len = demuxer_get_time_length(demuxer);
    if (len >= 0)
        return len;

    // Unknown
    return 0;
}

/* If there are timestamps from stream level then use those (for example
 * DVDs can have consistent times there while the MPEG-level timestamps
 * reset). */
double get_current_time(struct MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->demuxer;
    if (!demuxer)
        return 0;
    if (demuxer->stream_pts != MP_NOPTS_VALUE)
        return demuxer->stream_pts;
    if (mpctx->playback_pts != MP_NOPTS_VALUE)
        return mpctx->playback_pts;
    if (mpctx->last_seek_pts != MP_NOPTS_VALUE)
        return mpctx->last_seek_pts;
    return 0;
}

double get_start_time(struct MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->demuxer;
    if (!demuxer)
        return 0;
    return demuxer_get_start_time(demuxer);
}

// Return playback position in 0.0-1.0 ratio, or -1 if unknown.
double get_current_pos_ratio(struct MPContext *mpctx, bool use_range)
{
    struct demuxer *demuxer = mpctx->demuxer;
    if (!demuxer)
        return -1;
    double ans = -1;
    double start = get_start_time(mpctx);
    double len = get_time_length(mpctx);
    if (use_range) {
        double startpos = rel_time_to_abs(mpctx, mpctx->opts->play_start,
                MP_NOPTS_VALUE);
        double endpos = get_play_end_pts(mpctx);
        if (endpos == MP_NOPTS_VALUE || endpos > start + len)
            endpos = start + len;
        if (startpos == MP_NOPTS_VALUE || startpos < start)
            startpos = start;
        if (endpos < startpos)
            endpos = startpos;
        start = startpos;
        len = endpos - startpos;
    }
    double pos = get_current_time(mpctx);
    if (len > 0 && !demuxer->ts_resets_possible) {
        ans = av_clipf((pos - start) / len, 0, 1);
    } else {
        int64_t size = (demuxer->movi_end - demuxer->movi_start);
        int64_t fpos = demuxer->filepos > 0 ?
                       demuxer->filepos : stream_tell(demuxer->stream);
        if (size > 0)
            ans = av_clipf((double)(fpos - demuxer->movi_start) / size, 0, 1);
    }
    if (use_range) {
        if (mpctx->opts->play_frames > 0)
            ans = max(ans, 1.0 -
                    mpctx->max_frames / (double) mpctx->opts->play_frames);
    }
    return ans;
}

int get_percent_pos(struct MPContext *mpctx)
{
    return av_clip(get_current_pos_ratio(mpctx, false) * 100, 0, 100);
}

// -2 is no chapters, -1 is before first chapter
int get_current_chapter(struct MPContext *mpctx)
{
    double current_pts = get_current_time(mpctx);
    if (mpctx->chapters) {
        int i;
        for (i = 1; i < mpctx->num_chapters; i++)
            if (current_pts < mpctx->chapters[i].start)
                break;
        return FFMAX(mpctx->last_chapter_seek, i - 1);
    }
    if (mpctx->master_demuxer)
        return FFMAX(mpctx->last_chapter_seek,
                demuxer_get_current_chapter(mpctx->master_demuxer, current_pts));
    return -2;
}

char *chapter_display_name(struct MPContext *mpctx, int chapter)
{
    char *name = chapter_name(mpctx, chapter);
    char *dname = name;
    if (name) {
        dname = talloc_asprintf(NULL, "(%d) %s", chapter + 1, name);
    } else if (chapter < -1) {
        dname = talloc_strdup(NULL, "(unavailable)");
    } else {
        int chapter_count = get_chapter_count(mpctx);
        if (chapter_count <= 0)
            dname = talloc_asprintf(NULL, "(%d)", chapter + 1);
        else
            dname = talloc_asprintf(NULL, "(%d) of %d", chapter + 1,
                                    chapter_count);
    }
    if (dname != name)
        talloc_free(name);
    return dname;
}

// returns NULL if chapter name unavailable
char *chapter_name(struct MPContext *mpctx, int chapter)
{
    if (mpctx->chapters) {
        if (chapter < 0 || chapter >= mpctx->num_chapters)
            return NULL;
        return talloc_strdup(NULL, mpctx->chapters[chapter].name);
    }
    if (mpctx->master_demuxer)
        return demuxer_chapter_name(mpctx->master_demuxer, chapter);
    return NULL;
}

// returns the start of the chapter in seconds (-1 if unavailable)
double chapter_start_time(struct MPContext *mpctx, int chapter)
{
    if (chapter == -1)
        return get_start_time(mpctx);
    if (mpctx->chapters)
        return mpctx->chapters[chapter].start;
    if (mpctx->master_demuxer)
        return demuxer_chapter_time(mpctx->master_demuxer, chapter);
    return -1;
}

int get_chapter_count(struct MPContext *mpctx)
{
    if (mpctx->chapters)
        return mpctx->num_chapters;
    if (mpctx->master_demuxer)
        return demuxer_chapter_count(mpctx->master_demuxer);
    return 0;
}


int mp_get_cache_percent(struct MPContext *mpctx)
{
    if (mpctx->stream) {
        int64_t size = -1;
        int64_t fill = -1;
        stream_control(mpctx->stream, STREAM_CTRL_GET_CACHE_SIZE, &size);
        stream_control(mpctx->stream, STREAM_CTRL_GET_CACHE_FILL, &fill);
        if (size > 0 && fill >= 0)
            return fill / (size / 100);
    }
    return -1;
}

static bool mp_get_cache_idle(struct MPContext *mpctx)
{
    int idle = 0;
    if (mpctx->stream)
        stream_control(mpctx->stream, STREAM_CTRL_GET_CACHE_IDLE, &idle);
    return idle;
}



static void print_resolve_contents(struct mp_log *log,
                                   struct mp_resolve_result *res)
{
    mp_msg_log(log, MSGL_V, "Resolve:\n");
    mp_msg_log(log, MSGL_V, "  title: %s\n", res->title);
    mp_msg_log(log, MSGL_V, "  url: %s\n", res->url);
    for (int n = 0; n < res->num_srcs; n++) {
        mp_msg_log(log, MSGL_V, "  source %d:\n", n);
        if (res->srcs[n]->url)
            mp_msg_log(log, MSGL_V, "    url: %s\n", res->srcs[n]->url);
        if (res->srcs[n]->encid)
            mp_msg_log(log, MSGL_V, "    encid: %s\n", res->srcs[n]->encid);
    }
    for (int n = 0; n < res->num_subs; n++) {
        mp_msg_log(log, MSGL_V, "  subtitle %d:\n", n);
        if (res->subs[n]->url)
            mp_msg_log(log, MSGL_V, "    url: %s\n", res->subs[n]->url);
        if (res->subs[n]->lang)
            mp_msg_log(log, MSGL_V, "    lang: %s\n", res->subs[n]->lang);
        if (res->subs[n]->data) {
            mp_msg_log(log, MSGL_V, "    data: %zd bytes\n",
                       strlen(res->subs[n]->data));
        }
    }
    if (res->playlist) {
        mp_msg_log(log, MSGL_V, "  playlist with %d entries\n",
                   playlist_entry_count(res->playlist));
    }
}


static void print_timeline(struct MPContext *mpctx)
{
    if (mpctx->timeline) {
        int part_count = mpctx->num_timeline_parts;
        MP_VERBOSE(mpctx, "Timeline contains %d parts from %d "
                   "sources. Total length %.3f seconds.\n", part_count,
                   mpctx->num_sources, mpctx->timeline[part_count].start);
        MP_VERBOSE(mpctx, "Source files:\n");
        for (int i = 0; i < mpctx->num_sources; i++)
            MP_VERBOSE(mpctx, "%d: %s\n", i,
                       mpctx->sources[i]->filename);
        MP_VERBOSE(mpctx, "Timeline parts: (number, start, "
               "source_start, source):\n");
        for (int i = 0; i < part_count; i++) {
            struct timeline_part *p = mpctx->timeline + i;
            MP_VERBOSE(mpctx, "%3d %9.3f %9.3f %p/%s\n", i, p->start,
                       p->source_start, p->source, p->source->filename);
        }
        MP_VERBOSE(mpctx, "END %9.3f\n",
                   mpctx->timeline[part_count].start);
    }
}
