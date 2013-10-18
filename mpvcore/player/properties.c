
static char *format_bitrate(int rate)
{
    return talloc_asprintf(NULL, "%d kbps", rate * 8 / 1000);
}

static char *format_delay(double time)
{
    return talloc_asprintf(NULL, "%d ms", ROUND(time * 1000));
}

// Property-option bridge.
static int mp_property_generic_option(struct m_option *prop, int action,
                                      void *arg, MPContext *mpctx)
{
    char *optname = prop->priv;
    struct m_config_option *opt = m_config_get_co(mpctx->mconfig,
                                                  bstr0(optname));
    void *valptr = opt->data;

    switch (action) {
    case M_PROPERTY_GET_TYPE:
        *(struct m_option *)arg = *(opt->opt);
        return M_PROPERTY_OK;
    case M_PROPERTY_GET:
        m_option_copy(opt->opt, arg, valptr);
        return M_PROPERTY_OK;
    case M_PROPERTY_SET:
        m_option_copy(opt->opt, valptr, arg);
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Playback speed (RW)
static int mp_property_playback_speed(m_option_t *prop, int action,
                                      void *arg, MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    double orig_speed = opts->playback_speed;
    switch (action) {
    case M_PROPERTY_SET: {
        opts->playback_speed = *(double *) arg;
        // Adjust time until next frame flip for nosound mode
        mpctx->time_frame *= orig_speed / opts->playback_speed;
        if (mpctx->sh_audio)
            reinit_audio_chain(mpctx);
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_PRINT:
        *(char **)arg = talloc_asprintf(NULL, "x %6.2f", orig_speed);
        return M_PROPERTY_OK;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// filename with path (RO)
static int mp_property_path(m_option_t *prop, int action, void *arg,
                            MPContext *mpctx)
{
    if (!mpctx->filename)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_strdup_ro(prop, action, arg, mpctx->filename);
}

static int mp_property_filename(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    if (!mpctx->filename)
        return M_PROPERTY_UNAVAILABLE;
    char *filename = talloc_strdup(NULL, mpctx->filename);
    if (mp_is_url(bstr0(filename)))
        mp_url_unescape_inplace(filename);
    char *f = (char *)mp_basename(filename);
    int r = m_property_strdup_ro(prop, action, arg, f[0] ? f : filename);
    talloc_free(filename);
    return r;
}

static int mp_property_media_title(m_option_t *prop, int action, void *arg,
                                   MPContext *mpctx)
{
    char *name = NULL;
    if (mpctx->resolve_result)
        name = mpctx->resolve_result->title;
    if (name && name[0])
        return m_property_strdup_ro(prop, action, arg, name);
    if (mpctx->master_demuxer) {
        name = demux_info_get(mpctx->master_demuxer, "title");
        if (name && name[0])
            return m_property_strdup_ro(prop, action, arg, name);
    }
    return mp_property_filename(prop, action, arg, mpctx);
}

static int mp_property_stream_path(m_option_t *prop, int action, void *arg,
                                   MPContext *mpctx)
{
    struct stream *stream = mpctx->stream;
    if (!stream || !stream->url)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_strdup_ro(prop, action, arg, stream->url);
}

static int mp_property_stream_capture(m_option_t *prop, int action,
                                      void *arg, MPContext *mpctx)
{
    if (!mpctx->stream)
        return M_PROPERTY_UNAVAILABLE;

    if (action == M_PROPERTY_SET) {
        char *filename = *(char **)arg;
        stream_set_capture_file(mpctx->stream, filename);
        // fall through to mp_property_generic_option
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Demuxer name (RO)
static int mp_property_demuxer(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->master_demuxer;
    if (!demuxer)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_strdup_ro(prop, action, arg, demuxer->desc->name);
}

/// Position in the stream (RW)
static int mp_property_stream_pos(m_option_t *prop, int action, void *arg,
                                  MPContext *mpctx)
{
    struct stream *stream = mpctx->stream;
    if (!stream)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_GET:
        *(int64_t *) arg = stream_tell(stream);
        return M_PROPERTY_OK;
    case M_PROPERTY_SET:
        stream_seek(stream, *(int64_t *) arg);
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Stream start offset (RO)
static int mp_property_stream_start(m_option_t *prop, int action,
                                    void *arg, MPContext *mpctx)
{
    struct stream *stream = mpctx->stream;
    if (!stream)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int64_ro(prop, action, arg, stream->start_pos);
}

/// Stream end offset (RO)
static int mp_property_stream_end(m_option_t *prop, int action, void *arg,
                                  MPContext *mpctx)
{
    struct stream *stream = mpctx->stream;
    if (!stream)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int64_ro(prop, action, arg, stream->end_pos);
}

/// Stream length (RO)
static int mp_property_stream_length(m_option_t *prop, int action,
                                     void *arg, MPContext *mpctx)
{
    struct stream *stream = mpctx->stream;
    if (!stream)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int64_ro(prop, action, arg,
                               stream->end_pos - stream->start_pos);
}

// Does some magic to handle "<name>/full" as time formatted with milliseconds.
// Assumes prop is the type of the actual property.
static int property_time(m_option_t *prop, int action, void *arg, double time)
{
    switch (action) {
    case M_PROPERTY_GET:
        *(double *)arg = time;
        return M_PROPERTY_OK;
    case M_PROPERTY_KEY_ACTION: {
        struct m_property_action_arg *ka = arg;

        if (strcmp(ka->key, "full") != 0)
            return M_PROPERTY_UNKNOWN;

        switch (ka->action) {
        case M_PROPERTY_GET:
            *(double *)ka->arg = time;
            return M_PROPERTY_OK;
        case M_PROPERTY_PRINT:
            *(char **)ka->arg = mp_format_time(time, true);
            return M_PROPERTY_OK;
        case M_PROPERTY_GET_TYPE:
            *(struct m_option *)ka->arg = *prop;
            return M_PROPERTY_OK;
        }
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Current stream position in seconds (RO)
static int mp_property_stream_time_pos(m_option_t *prop, int action,
                                       void *arg, MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->demuxer;
    if (!demuxer)
        return M_PROPERTY_UNAVAILABLE;
    double pts = demuxer->stream_pts;
    if (pts == MP_NOPTS_VALUE)
        return M_PROPERTY_UNAVAILABLE;

    return property_time(prop, action, arg, pts);
}


/// Media length in seconds (RO)
static int mp_property_length(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    double len;

    if (!(int) (len = get_time_length(mpctx)))
        return M_PROPERTY_UNAVAILABLE;

    return property_time(prop, action, arg, len);
}

static int mp_property_avsync(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    if (!mpctx->sh_audio || !mpctx->sh_video)
        return M_PROPERTY_UNAVAILABLE;
    if (mpctx->last_av_difference == MP_NOPTS_VALUE)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_double_ro(prop, action, arg, mpctx->last_av_difference);
}

/// Current position in percent (RW)
static int mp_property_percent_pos(m_option_t *prop, int action,
                                   void *arg, MPContext *mpctx)
{
    if (!mpctx->num_sources)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_SET: ;
        double pos = *(double *)arg;
        queue_seek(mpctx, MPSEEK_FACTOR, pos / 100.0, 0);
        return M_PROPERTY_OK;
    case M_PROPERTY_GET:
        *(double *)arg = get_current_pos_ratio(mpctx, false) * 100.0;
        return M_PROPERTY_OK;
    case M_PROPERTY_PRINT:
        *(char **)arg = talloc_asprintf(NULL, "%d", get_percent_pos(mpctx));
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Current position in seconds (RW)
static int mp_property_time_pos(m_option_t *prop, int action,
                                void *arg, MPContext *mpctx)
{
    if (!mpctx->num_sources)
        return M_PROPERTY_UNAVAILABLE;

    if (action == M_PROPERTY_SET) {
        queue_seek(mpctx, MPSEEK_ABSOLUTE, *(double *)arg, 0);
        return M_PROPERTY_OK;
    }
    return property_time(prop, action, arg, get_current_time(mpctx));
}

static int mp_property_remaining(m_option_t *prop, int action,
                                 void *arg, MPContext *mpctx)
{
    double len = get_time_length(mpctx);
    double pos = get_current_time(mpctx);
    double start = get_start_time(mpctx);

    if (!(int)len)
        return M_PROPERTY_UNAVAILABLE;

    return property_time(prop, action, arg, len - (pos - start));
}

/// Current chapter (RW)
static int mp_property_chapter(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    int chapter = get_current_chapter(mpctx);
    if (chapter < -1)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_GET:
        *(int *) arg = chapter;
        return M_PROPERTY_OK;
    case M_PROPERTY_PRINT: {
        *(char **) arg = chapter_display_name(mpctx, chapter);
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_SWITCH:
    case M_PROPERTY_SET: ;
        int step_all;
        if (action == M_PROPERTY_SWITCH) {
            struct m_property_switch_arg *sarg = arg;
            step_all = ROUND(sarg->inc);
            // Check threshold for relative backward seeks
            if (mpctx->opts->chapter_seek_threshold >= 0 && step_all < 0) {
                double current_chapter_start =
                    chapter_start_time(mpctx, chapter);
                // If we are far enough into a chapter, seek back to the
                // beginning of current chapter instead of previous one
                if (current_chapter_start >= 0 &&
                    get_current_time(mpctx) - current_chapter_start >
                    mpctx->opts->chapter_seek_threshold)
                    step_all++;
            }
        } else // Absolute set
            step_all = *(int *)arg - chapter;
        chapter += step_all;
        if (chapter < -1)
            chapter = -1;
        if (chapter >= get_chapter_count(mpctx) && step_all > 0) {
            mpctx->stop_play = PT_NEXT_ENTRY;
        } else {
            mp_seek_chapter(mpctx, chapter);
        }
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static int mp_property_list_chapters(m_option_t *prop, int action, void *arg,
                                     MPContext *mpctx)
{
    if (action == M_PROPERTY_GET) {
        int count = get_chapter_count(mpctx);
        int cur = mpctx->num_sources ? get_current_chapter(mpctx) : -1;
        char *res = NULL;
        int n;

        if (count < 1) {
            res = talloc_asprintf_append(res, "No chapters.");
        }

        for (n = 0; n < count; n++) {
            char *name = chapter_display_name(mpctx, n);
            double t = chapter_start_time(mpctx, n);
            char* time = mp_format_time(t, false);
            res = talloc_asprintf_append(res, "%s", time);
            talloc_free(time);
            char *m1 = "> ", *m2 = " <";
            if (n != cur)
                m1 = m2 = "";
            res = talloc_asprintf_append(res, "   %s%s%s\n", m1, name, m2);
            talloc_free(name);
        }

        *(char **)arg = res;
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static int mp_property_edition(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct demuxer *demuxer = mpctx->master_demuxer;
    if (!demuxer)
        return M_PROPERTY_UNAVAILABLE;
    if (demuxer->num_editions <= 0)
        return M_PROPERTY_UNAVAILABLE;

    int edition = demuxer->edition;

    switch (action) {
    case M_PROPERTY_GET:
        *(int *)arg = edition;
        return M_PROPERTY_OK;
    case M_PROPERTY_SET: {
        edition = *(int *)arg;
        if (edition != demuxer->edition) {
            opts->edition_id = edition;
            mpctx->stop_play = PT_RESTART;
        }
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_GET_TYPE: {
        struct m_option opt = {
            .name = prop->name,
            .type = CONF_TYPE_INT,
            .flags = CONF_RANGE,
            .min = 0,
            .max = demuxer->num_editions - 1,
        };
        *(struct m_option *)arg = opt;
        return M_PROPERTY_OK;
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static struct mp_resolve_src *find_source(struct mp_resolve_result *res,
                                          char *encid, char *url)
{
    if (res->num_srcs == 0)
        return NULL;

    int src = 0;
    for (int n = 0; n < res->num_srcs; n++) {
        char *s_url = res->srcs[n]->url;
        char *s_encid = res->srcs[n]->encid;
        if (url && s_url && strcmp(url, s_url) == 0) {
            src = n;
            break;
        }
        // Prefer source URL if possible; so continue in case encid isn't unique
        if (encid && s_encid && strcmp(encid, s_encid) == 0)
            src = n;
    }
    return res->srcs[src];
}

static int mp_property_quvi_format(m_option_t *prop, int action, void *arg,
                                   MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct mp_resolve_result *res = mpctx->resolve_result;
    if (!res || !res->num_srcs)
        return M_PROPERTY_UNAVAILABLE;

    struct mp_resolve_src *cur = find_source(res, opts->quvi_format, res->url);
    if (!cur)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_GET:
        *(char **)arg = talloc_strdup(NULL, cur->encid);
        return M_PROPERTY_OK;
    case M_PROPERTY_SET: {
        mpctx->stop_play = PT_RESTART;
        // Make it restart at the same position. This will have disastrous
        // consequences if the stream is not arbitrarily seekable, but whatever.
        m_config_backup_opt(mpctx->mconfig, "start");
        opts->play_start = (struct m_rel_time) {
            .type = REL_TIME_ABSOLUTE,
            .pos = get_current_time(mpctx),
        };
        break;
    }
    case M_PROPERTY_SWITCH: {
        struct m_property_switch_arg *sarg = arg;
        int pos = 0;
        for (int n = 0; n < res->num_srcs; n++) {
            if (res->srcs[n] == cur) {
                pos = n;
                break;
            }
        }
        pos += sarg->inc;
        if (pos < 0 || pos >= res->num_srcs) {
            if (sarg->wrap) {
                pos = (res->num_srcs + pos) % res->num_srcs;
            } else {
                pos = av_clip(pos, 0, res->num_srcs);
            }
        }
        char *fmt = res->srcs[pos]->encid;
        return mp_property_quvi_format(prop, M_PROPERTY_SET, &fmt, mpctx);
    }
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Number of titles in file
static int mp_property_titles(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->master_demuxer;
    unsigned int num_titles;
    if (!demuxer || stream_control(demuxer->stream, STREAM_CTRL_GET_NUM_TITLES,
                                   &num_titles) < 1)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int_ro(prop, action, arg, num_titles);
}

/// Number of chapters in file
static int mp_property_chapters(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    if (!mpctx->num_sources)
        return M_PROPERTY_UNAVAILABLE;
    int count = get_chapter_count(mpctx);
    return m_property_int_ro(prop, action, arg, count);
}

static int mp_property_editions(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->master_demuxer;
    if (!demuxer)
        return M_PROPERTY_UNAVAILABLE;
    if (demuxer->num_editions <= 0)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int_ro(prop, action, arg, demuxer->num_editions);
}

/// Current dvd angle (RW)
static int mp_property_angle(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->master_demuxer;
    int angle = -1;
    int angles;

    if (demuxer)
        angle = demuxer_get_current_angle(demuxer);
    if (angle < 0)
        return M_PROPERTY_UNAVAILABLE;
    angles = demuxer_angles_count(demuxer);
    if (angles <= 1)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_GET:
        *(int *) arg = angle;
        return M_PROPERTY_OK;
    case M_PROPERTY_PRINT: {
        *(char **) arg = talloc_asprintf(NULL, "%d/%d", angle, angles);
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_SET:
        angle = demuxer_set_angle(demuxer, *(int *)arg);
        if (angle >= 0) {
            if (mpctx->sh_video)
                resync_video_stream(mpctx->sh_video);

            if (mpctx->sh_audio)
                resync_audio_stream(mpctx->sh_audio);
        }
        return M_PROPERTY_OK;
    case M_PROPERTY_GET_TYPE: {
        struct m_option opt = {
            .name = prop->name,
            .type = CONF_TYPE_INT,
            .flags = CONF_RANGE,
            .min = 1,
            .max = angles,
        };
        *(struct m_option *)arg = opt;
        return M_PROPERTY_OK;
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static int tag_property(m_option_t *prop, int action, void *arg,
                        struct mp_tags *tags)
{
    static const m_option_t key_type =
    {
        "tags", NULL, CONF_TYPE_STRING, 0, 0, 0, NULL
    };

    switch (action) {
    case M_PROPERTY_GET: {
        char **slist = NULL;
        int num = 0;
        for (int n = 0; n < tags->num_keys; n++) {
            MP_TARRAY_APPEND(NULL, slist, num, tags->keys[n]);
            MP_TARRAY_APPEND(NULL, slist, num, tags->values[n]);
        }
        MP_TARRAY_APPEND(NULL, slist, num, NULL);
        *(char ***)arg = slist;
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_PRINT: {
        char *res = NULL;
        for (int n = 0; n < tags->num_keys; n++) {
            res = talloc_asprintf_append_buffer(res, "%s: %s\n",
                                                tags->keys[n], tags->values[n]);
        }
        *(char **)arg = res;
        return res ? M_PROPERTY_OK : M_PROPERTY_UNAVAILABLE;
    }
    case M_PROPERTY_KEY_ACTION: {
        struct m_property_action_arg *ka = arg;
        char *meta = mp_tags_get_str(tags, ka->key);
        if (!meta)
            return M_PROPERTY_UNKNOWN;
        switch (ka->action) {
        case M_PROPERTY_GET:
            *(char **)ka->arg = talloc_strdup(NULL, meta);
            return M_PROPERTY_OK;
        case M_PROPERTY_GET_TYPE:
            *(struct m_option *)ka->arg = key_type;
            return M_PROPERTY_OK;
        }
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Demuxer meta data
static int mp_property_metadata(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->master_demuxer;
    if (!demuxer)
        return M_PROPERTY_UNAVAILABLE;

    return tag_property(prop, action, arg, demuxer->metadata);
}

static int mp_property_chapter_metadata(m_option_t *prop, int action, void *arg,
                                        MPContext *mpctx)
{
    struct demuxer *demuxer = mpctx->master_demuxer;
    int chapter = get_current_chapter(mpctx);
    if (!demuxer || chapter < 0)
        return M_PROPERTY_UNAVAILABLE;

    assert(chapter < demuxer->num_chapters);

    return tag_property(prop, action, arg, demuxer->chapters[chapter].metadata);
}

static int mp_property_pause(m_option_t *prop, int action, void *arg,
                             void *ctx)
{
    MPContext *mpctx = ctx;

    if (action == M_PROPERTY_SET) {
        if (*(int *)arg) {
            pause_player(mpctx);
        } else {
            unpause_player(mpctx);
        }
        return M_PROPERTY_OK;
    }
    return mp_property_generic_option(prop, action, arg, ctx);
}

static int mp_property_cache(m_option_t *prop, int action, void *arg,
                             void *ctx)
{
    MPContext *mpctx = ctx;
    int cache = mp_get_cache_percent(mpctx);
    if (cache < 0)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int_ro(prop, action, arg, cache);
}

static int mp_property_clock(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    char outstr[6];
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);

    if ((tmp != NULL) && (strftime(outstr, sizeof(outstr), "%H:%M", tmp) == 5))
        return m_property_strdup_ro(prop, action, arg, outstr);
    return M_PROPERTY_UNAVAILABLE;
}

/// Volume (RW)
static int mp_property_volume(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    switch (action) {
    case M_PROPERTY_GET:
        mixer_getbothvolume(mpctx->mixer, arg);
        return M_PROPERTY_OK;
    case M_PROPERTY_SET:
        if (!mixer_audio_initialized(mpctx->mixer))
            return M_PROPERTY_ERROR;
        mixer_setvolume(mpctx->mixer, *(float *) arg, *(float *) arg);
        return M_PROPERTY_OK;
    case M_PROPERTY_SWITCH: {
        if (!mixer_audio_initialized(mpctx->mixer))
            return M_PROPERTY_ERROR;
        struct m_property_switch_arg *sarg = arg;
        if (sarg->inc <= 0)
            mixer_decvolume(mpctx->mixer);
        else
            mixer_incvolume(mpctx->mixer);
        return M_PROPERTY_OK;
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Mute (RW)
static int mp_property_mute(m_option_t *prop, int action, void *arg,
                            MPContext *mpctx)
{
    switch (action) {
    case M_PROPERTY_SET:
        if (!mixer_audio_initialized(mpctx->mixer))
            return M_PROPERTY_ERROR;
        mixer_setmute(mpctx->mixer, *(int *) arg);
        return M_PROPERTY_OK;
    case M_PROPERTY_GET:
        *(int *)arg =  mixer_getmute(mpctx->mixer);
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static int mp_property_volrestore(m_option_t *prop, int action,
                                   void *arg, MPContext *mpctx)
{
    switch (action) {
    case M_PROPERTY_GET: {
        char *s = mixer_get_volume_restore_data(mpctx->mixer);
        *(char **)arg = s;
        return s ? M_PROPERTY_OK : M_PROPERTY_UNAVAILABLE;
    }
    case M_PROPERTY_SET:
        return M_PROPERTY_NOT_IMPLEMENTED;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Audio delay (RW)
static int mp_property_audio_delay(m_option_t *prop, int action,
                                   void *arg, MPContext *mpctx)
{
    if (!(mpctx->sh_audio && mpctx->sh_video))
        return M_PROPERTY_UNAVAILABLE;
    float delay = mpctx->opts->audio_delay;
    switch (action) {
    case M_PROPERTY_PRINT:
        *(char **)arg = format_delay(delay);
        return M_PROPERTY_OK;
    case M_PROPERTY_SET:
        mpctx->audio_delay = mpctx->opts->audio_delay = *(float *)arg;
        mpctx->delay -= mpctx->audio_delay - delay;
        return M_PROPERTY_OK;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Audio codec tag (RO)
static int mp_property_audio_format(m_option_t *prop, int action,
                                    void *arg, MPContext *mpctx)
{
    const char *c = mpctx->sh_audio ? mpctx->sh_audio->gsh->codec : NULL;
    return m_property_strdup_ro(prop, action, arg, c);
}

/// Audio codec name (RO)
static int mp_property_audio_codec(m_option_t *prop, int action,
                                   void *arg, MPContext *mpctx)
{
    const char *c = mpctx->sh_audio ? mpctx->sh_audio->gsh->decoder_desc : NULL;
    return m_property_strdup_ro(prop, action, arg, c);
}

/// Audio bitrate (RO)
static int mp_property_audio_bitrate(m_option_t *prop, int action,
                                     void *arg, MPContext *mpctx)
{
    if (!mpctx->sh_audio)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_PRINT:
        *(char **)arg = format_bitrate(mpctx->sh_audio->i_bps);
        return M_PROPERTY_OK;
    case M_PROPERTY_GET:
        *(int *)arg = mpctx->sh_audio->i_bps;
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Samplerate (RO)
static int mp_property_samplerate(m_option_t *prop, int action, void *arg,
                                  MPContext *mpctx)
{
    if (!mpctx->sh_audio)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_PRINT:
        *(char **)arg = talloc_asprintf(NULL, "%d kHz",
                                        mpctx->sh_audio->samplerate / 1000);
        return M_PROPERTY_OK;
    case M_PROPERTY_GET:
        *(int *)arg = mpctx->sh_audio->samplerate;
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Number of channels (RO)
static int mp_property_channels(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    if (!mpctx->sh_audio)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_PRINT:
        *(char **) arg = mp_chmap_to_str(&mpctx->sh_audio->channels);
        return M_PROPERTY_OK;
    case M_PROPERTY_GET:
        *(int *)arg = mpctx->sh_audio->channels.num;
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Balance (RW)
static int mp_property_balance(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    float bal;

    switch (action) {
    case M_PROPERTY_GET:
        mixer_getbalance(mpctx->mixer, arg);
        return M_PROPERTY_OK;
    case M_PROPERTY_PRINT: {
        char **str = arg;
        mixer_getbalance(mpctx->mixer, &bal);
        if (bal == 0.f)
            *str = talloc_strdup(NULL, "center");
        else if (bal == -1.f)
            *str = talloc_strdup(NULL, "left only");
        else if (bal == 1.f)
            *str = talloc_strdup(NULL, "right only");
        else {
            unsigned right = (bal + 1.f) / 2.f * 100.f;
            *str = talloc_asprintf(NULL, "left %d%%, right %d%%",
                                   100 - right, right);
        }
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_SET:
        mixer_setbalance(mpctx->mixer, *(float *)arg);
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static struct track* track_next(struct MPContext *mpctx, enum stream_type type,
                                int direction, struct track *track)
{
    assert(direction == -1 || direction == +1);
    struct track *prev = NULL, *next = NULL;
    bool seen = track == NULL;
    for (int n = 0; n < mpctx->num_tracks; n++) {
        struct track *cur = mpctx->tracks[n];
        if (cur->type == type) {
            if (cur == track) {
                seen = true;
            } else {
                if (seen && !next) {
                    next = cur;
                }
                if (!seen || !track) {
                    prev = cur;
                }
            }
        }
    }
    return direction > 0 ? next : prev;
}

static int property_switch_track(m_option_t *prop, int action, void *arg,
                                 MPContext *mpctx, enum stream_type type)
{
    if (!mpctx->num_sources)
        return M_PROPERTY_UNAVAILABLE;
    struct track *track = mpctx->current_track[type];

    switch (action) {
    case M_PROPERTY_GET:
        *(int *) arg = track ? track->user_tid : -2;
        return M_PROPERTY_OK;
    case M_PROPERTY_PRINT:
        if (!track)
            *(char **) arg = talloc_strdup(NULL, "no");
        else {
            char *lang = track->lang;
            if (!lang)
                lang = mp_gtext("unknown");

            if (track->title)
                *(char **)arg = talloc_asprintf(NULL, "(%d) %s (\"%s\")",
                                           track->user_tid, lang, track->title);
            else
                *(char **)arg = talloc_asprintf(NULL, "(%d) %s",
                                                track->user_tid, lang);
        }
        return M_PROPERTY_OK;

    case M_PROPERTY_SWITCH: {
        struct m_property_switch_arg *sarg = arg;
        mp_switch_track(mpctx, type,
            track_next(mpctx, type, sarg->inc >= 0 ? +1 : -1, track));
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_SET:
        mp_switch_track(mpctx, type, mp_track_by_tid(mpctx, type, *(int *)arg));
        return M_PROPERTY_OK;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

static const char *track_type_name(enum stream_type t)
{
    switch (t) {
    case STREAM_VIDEO: return "Video";
    case STREAM_AUDIO: return "Audio";
    case STREAM_SUB: return "Sub";
    }
    return NULL;
}

static int property_list_tracks(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    if (action == M_PROPERTY_GET) {
        char *res = NULL;

        for (int type = 0; type < STREAM_TYPE_COUNT; type++) {
            for (int n = 0; n < mpctx->num_tracks; n++) {
                struct track *track = mpctx->tracks[n];
                if (track->type != type)
                    continue;

                bool selected = mpctx->current_track[track->type] == track;
                res = talloc_asprintf_append(res, "%s: ",
                                             track_type_name(track->type));
                if (selected)
                    res = talloc_asprintf_append(res, "> ");
                res = talloc_asprintf_append(res, "(%d) ", track->user_tid);
                if (track->title)
                    res = talloc_asprintf_append(res, "'%s' ", track->title);
                if (track->lang)
                    res = talloc_asprintf_append(res, "(%s) ", track->lang);
                if (track->is_external)
                    res = talloc_asprintf_append(res, "(external) ");
                if (selected)
                    res = talloc_asprintf_append(res, "<");
                res = talloc_asprintf_append(res, "\n");
            }

            res = talloc_asprintf_append(res, "\n");
        }

        struct demuxer *demuxer = mpctx->master_demuxer;
        if (demuxer && demuxer->num_editions > 1)
            res = talloc_asprintf_append(res, "\nEdition: %d of %d\n",
                                        demuxer->edition + 1,
                                        demuxer->num_editions);

        *(char **)arg = res;
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

/// Selected audio id (RW)
static int mp_property_audio(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    return property_switch_track(prop, action, arg, mpctx, STREAM_AUDIO);
}

/// Selected video id (RW)
static int mp_property_video(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    return property_switch_track(prop, action, arg, mpctx, STREAM_VIDEO);
}

static struct track *find_track_by_demuxer_id(MPContext *mpctx,
                                              enum stream_type type,
                                              int demuxer_id)
{
    for (int n = 0; n < mpctx->num_tracks; n++) {
        struct track *track = mpctx->tracks[n];
        if (track->type == type && track->demuxer_id == demuxer_id)
            return track;
    }
    return NULL;
}

static int mp_property_program(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    demux_program_t prog;

    struct demuxer *demuxer = mpctx->master_demuxer;
    if (!demuxer)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_SWITCH:
    case M_PROPERTY_SET:
        if (action == M_PROPERTY_SET && arg)
            prog.progid = *((int *) arg);
        else
            prog.progid = -1;
        if (demux_control(demuxer, DEMUXER_CTRL_IDENTIFY_PROGRAM, &prog) ==
            DEMUXER_CTRL_NOTIMPL)
            return M_PROPERTY_ERROR;

        if (prog.aid < 0 && prog.vid < 0) {
            mp_msg(MSGT_CPLAYER, MSGL_ERR,
                   "Selected program contains no audio or video streams!\n");
            return M_PROPERTY_ERROR;
        }
        mp_switch_track(mpctx, STREAM_VIDEO,
                find_track_by_demuxer_id(mpctx, STREAM_VIDEO, prog.vid));
        mp_switch_track(mpctx, STREAM_AUDIO,
                find_track_by_demuxer_id(mpctx, STREAM_AUDIO, prog.aid));
        mp_switch_track(mpctx, STREAM_SUB,
                find_track_by_demuxer_id(mpctx, STREAM_VIDEO, prog.sid));
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}


/// Fullscreen state (RW)
static int mp_property_fullscreen(m_option_t *prop,
                                  int action,
                                  void *arg,
                                  MPContext *mpctx)
{
    if (!mpctx->video_out)
        return M_PROPERTY_UNAVAILABLE;
    struct mp_vo_opts *opts = mpctx->video_out->opts;

    if (action == M_PROPERTY_SET) {
        int val = *(int *)arg;
        opts->fullscreen = val;
        if (mpctx->video_out->config_ok)
            vo_control(mpctx->video_out, VOCTRL_FULLSCREEN, 0);
        return opts->fullscreen == val ? M_PROPERTY_OK : M_PROPERTY_ERROR;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

#define VF_DEINTERLACE_LABEL "deinterlace"

static const char *deint_filters[] = {
#ifdef CONFIG_VF_LAVFI
    "lavfi=yadif",
#endif
    "yadif",
#if CONFIG_VAAPI_VPP
    "vavpp",
#endif
    NULL
};

static int probe_deint_filters(struct MPContext *mpctx, const char *cmd)
{
    for (int n = 0; deint_filters[n]; n++) {
        char filter[80];
        // add a label so that removing the filter is easier
        snprintf(filter, sizeof(filter), "@%s:%s", VF_DEINTERLACE_LABEL,
                 deint_filters[n]);
        if (edit_filters(mpctx, STREAM_VIDEO, cmd, filter) >= 0)
            return 0;
    }
    return -1;
}

static int get_deinterlacing(struct MPContext *mpctx)
{
    vf_instance_t *vf = mpctx->sh_video->vfilter;
    int enabled = 0;
    if (vf->control(vf, VFCTRL_GET_DEINTERLACE, &enabled) != CONTROL_OK)
        enabled = -1;
    if (enabled < 0) {
        // vf_lavfi doesn't support VFCTRL_GET_DEINTERLACE
        if (vf_find_by_label(vf, VF_DEINTERLACE_LABEL))
            enabled = 1;
    }
    return enabled;
}

static void set_deinterlacing(struct MPContext *mpctx, bool enable)
{
    vf_instance_t *vf = mpctx->sh_video->vfilter;
    if (vf_find_by_label(vf, VF_DEINTERLACE_LABEL)) {
        if (!enable)
            edit_filters(mpctx, STREAM_VIDEO, "del", "@" VF_DEINTERLACE_LABEL);
    } else {
        if ((get_deinterlacing(mpctx) > 0) != enable) {
            int arg = enable;
            if (vf->control(vf, VFCTRL_SET_DEINTERLACE, &arg) != CONTROL_OK)
                probe_deint_filters(mpctx, "pre");
        }
    }
    mpctx->opts->deinterlace = get_deinterlacing(mpctx) > 0;
}

static int mp_property_deinterlace(m_option_t *prop, int action,
                                   void *arg, MPContext *mpctx)
{
    if (!mpctx->sh_video || !mpctx->sh_video->vfilter)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_GET:
        *(int *)arg = get_deinterlacing(mpctx) > 0;
        return M_PROPERTY_OK;
    case M_PROPERTY_SET:
        set_deinterlacing(mpctx, *(int *)arg);
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

// Generic option + requires hard refresh to make changes take effect.
static int video_refresh_property_helper(m_option_t *prop, int action,
                                         void *arg, MPContext *mpctx)
{
    int r = mp_property_generic_option(prop, action, arg, mpctx);
    if (action == M_PROPERTY_SET) {
        if (mpctx->sh_video) {
            reinit_video_filters(mpctx);
            mp_force_video_refresh(mpctx);
        }
    }
    return r;
}

static int mp_property_colormatrix(m_option_t *prop, int action, void *arg,
                                   MPContext *mpctx)
{
    if (action != M_PROPERTY_PRINT)
        return video_refresh_property_helper(prop, action, arg, mpctx);

    struct MPOpts *opts = mpctx->opts;

    struct mp_csp_details vo_csp = {0};
    if (mpctx->video_out)
        vo_control(mpctx->video_out, VOCTRL_GET_YUV_COLORSPACE, &vo_csp);

    struct mp_image_params vd_csp = {0};
    if (mpctx->sh_video)
        vd_control(mpctx->sh_video, VDCTRL_GET_PARAMS, &vd_csp);

    char *res = talloc_asprintf(NULL, "%s",
                                mp_csp_names[opts->requested_colorspace]);
    if (!vo_csp.format) {
        res = talloc_asprintf_append(res, " (VO: unknown)");
    } else if (vo_csp.format != opts->requested_colorspace) {
        res = talloc_asprintf_append(res, " (VO: %s)",
                                     mp_csp_names[vo_csp.format]);
    }
    if (!vd_csp.colorspace) {
        res = talloc_asprintf_append(res, " (VD: unknown)");
    } else if (!vo_csp.format || vd_csp.colorspace != vo_csp.format) {
        res = talloc_asprintf_append(res, " (VD: %s)",
                                     mp_csp_names[vd_csp.colorspace]);
    }
    *(char **)arg = res;
    return M_PROPERTY_OK;
}

static int mp_property_colormatrix_input_range(m_option_t *prop, int action,
                                               void *arg, MPContext *mpctx)
{
    if (action != M_PROPERTY_PRINT)
        return video_refresh_property_helper(prop, action, arg, mpctx);

    struct MPOpts *opts = mpctx->opts;

    struct mp_csp_details vo_csp = {0};
    if (mpctx->video_out)
        vo_control(mpctx->video_out, VOCTRL_GET_YUV_COLORSPACE, &vo_csp );

    struct mp_image_params vd_csp = {0};
    if (mpctx->sh_video)
        vd_control(mpctx->sh_video, VDCTRL_GET_PARAMS, &vd_csp);

    char *res = talloc_asprintf(NULL, "%s",
                                mp_csp_levels_names[opts->requested_input_range]);
    if (!vo_csp.levels_in) {
        res = talloc_asprintf_append(res, " (VO: unknown)");
    } else if (vo_csp.levels_in != opts->requested_input_range) {
        res = talloc_asprintf_append(res, " (VO: %s)",
                                     mp_csp_levels_names[vo_csp.levels_in]);
    }
    if (!vd_csp.colorlevels) {
        res = talloc_asprintf_append(res, " (VD: unknown)");
    } else if (!vo_csp.levels_in || vd_csp.colorlevels != vo_csp.levels_in) {
        res = talloc_asprintf_append(res, " (VD: %s)",
                                     mp_csp_levels_names[vd_csp.colorlevels]);
    }
    *(char **)arg = res;
    return M_PROPERTY_OK;
}

static int mp_property_colormatrix_output_range(m_option_t *prop, int action,
                                                void *arg, MPContext *mpctx)
{
    if (action != M_PROPERTY_PRINT)
        return video_refresh_property_helper(prop, action, arg, mpctx);

    struct MPOpts *opts = mpctx->opts;

    int req = opts->requested_output_range;
    struct mp_csp_details actual = {0};
    if (mpctx->video_out)
        vo_control(mpctx->video_out, VOCTRL_GET_YUV_COLORSPACE, &actual);

    char *res = talloc_asprintf(NULL, "%s", mp_csp_levels_names[req]);
    if (!actual.levels_out) {
        res = talloc_asprintf_append(res, " (Actual: unknown)");
    } else if (actual.levels_out != req) {
        res = talloc_asprintf_append(res, " (Actual: %s)",
                                     mp_csp_levels_names[actual.levels_out]);
    }
    *(char **)arg = res;
    return M_PROPERTY_OK;
}

// Update options which are managed through VOCTRL_GET/SET_PANSCAN.
static int panscan_property_helper(m_option_t *prop, int action, void *arg,
                                   MPContext *mpctx)
{

    if (!mpctx->video_out
        || vo_control(mpctx->video_out, VOCTRL_GET_PANSCAN, NULL) != VO_TRUE)
        return M_PROPERTY_UNAVAILABLE;

    int r = mp_property_generic_option(prop, action, arg, mpctx);
    if (action == M_PROPERTY_SET)
        vo_control(mpctx->video_out, VOCTRL_SET_PANSCAN, NULL);
    return r;
}

/// Helper to set vo flags.
/** \ingroup PropertyImplHelper
 */
static int mp_property_vo_flag(m_option_t *prop, int action, void *arg,
                               int vo_ctrl, int *vo_var, MPContext *mpctx)
{

    if (!mpctx->video_out)
        return M_PROPERTY_UNAVAILABLE;

    if (action == M_PROPERTY_SET) {
        if (*vo_var == !!*(int *) arg)
            return M_PROPERTY_OK;
        if (mpctx->video_out->config_ok)
            vo_control(mpctx->video_out, vo_ctrl, 0);
        return M_PROPERTY_OK;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Window always on top (RW)
static int mp_property_ontop(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    return mp_property_vo_flag(prop, action, arg, VOCTRL_ONTOP,
                               &mpctx->opts->vo.ontop, mpctx);
}

/// Show window borders (RW)
static int mp_property_border(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    return mp_property_vo_flag(prop, action, arg, VOCTRL_BORDER,
                               &mpctx->opts->vo.border, mpctx);
}

static int mp_property_framedrop(m_option_t *prop, int action,
                                 void *arg, MPContext *mpctx)
{
    if (!mpctx->sh_video)
        return M_PROPERTY_UNAVAILABLE;

    return mp_property_generic_option(prop, action, arg, mpctx);
}

static int mp_property_video_color(m_option_t *prop, int action, void *arg,
                                   MPContext *mpctx)
{
    if (!mpctx->sh_video)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_SET: {
        if (set_video_colors(mpctx->sh_video, prop->name, *(int *) arg) <= 0)
            return M_PROPERTY_UNAVAILABLE;
        break;
    }
    case M_PROPERTY_GET:
        if (get_video_colors(mpctx->sh_video, prop->name, (int *)arg) <= 0)
            return M_PROPERTY_UNAVAILABLE;
        // Write new value to option variable
        mp_property_generic_option(prop, M_PROPERTY_SET, arg, mpctx);
        return M_PROPERTY_OK;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Video codec tag (RO)
static int mp_property_video_format(m_option_t *prop, int action,
                                    void *arg, MPContext *mpctx)
{
    const char *c = mpctx->sh_video ? mpctx->sh_video->gsh->codec : NULL;
    return m_property_strdup_ro(prop, action, arg, c);
}

/// Video codec name (RO)
static int mp_property_video_codec(m_option_t *prop, int action,
                                   void *arg, MPContext *mpctx)
{
    const char *c = mpctx->sh_video ? mpctx->sh_video->gsh->decoder_desc : NULL;
    return m_property_strdup_ro(prop, action, arg, c);
}


/// Video bitrate (RO)
static int mp_property_video_bitrate(m_option_t *prop, int action,
                                     void *arg, MPContext *mpctx)
{
    if (!mpctx->sh_video)
        return M_PROPERTY_UNAVAILABLE;
    if (action == M_PROPERTY_PRINT) {
        *(char **)arg = format_bitrate(mpctx->sh_video->i_bps);
        return M_PROPERTY_OK;
    }
    return m_property_int_ro(prop, action, arg, mpctx->sh_video->i_bps);
}

/// Video display width (RO)
static int mp_property_width(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    struct sh_video *sh = mpctx->sh_video;
    if (!sh)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int_ro(prop, action, arg,
                             sh->vf_input ? sh->vf_input->w : sh->disp_w);
}

/// Video display height (RO)
static int mp_property_height(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    struct sh_video *sh = mpctx->sh_video;
    if (!sh)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int_ro(prop, action, arg,
                             sh->vf_input ? sh->vf_input->h : sh->disp_h);
}

static int property_vo_wh(m_option_t *prop, int action, void *arg,
                          MPContext *mpctx, bool get_w)
{
    struct vo *vo = mpctx->video_out;
    if (!mpctx->sh_video && !vo || !vo->hasframe)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_int_ro(prop, action, arg,
                             get_w ? vo->aspdat.prew : vo->aspdat.preh);
}

static int mp_property_dwidth(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    return property_vo_wh(prop, action, arg, mpctx, true);
}

static int mp_property_dheight(m_option_t *prop, int action, void *arg,
                                 MPContext *mpctx)
{
    return property_vo_wh(prop, action, arg, mpctx, false);
}

static int mp_property_osd_w(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    return m_property_int_ro(prop, action, arg, mpctx->osd->last_vo_res.w);
}

static int mp_property_osd_h(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    return m_property_int_ro(prop, action, arg, mpctx->osd->last_vo_res.w);
}

static int mp_property_osd_par(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    return m_property_double_ro(prop, action, arg,
                                mpctx->osd->last_vo_res.display_par);
}

/// Video fps (RO)
static int mp_property_fps(m_option_t *prop, int action, void *arg,
                           MPContext *mpctx)
{
    if (!mpctx->sh_video)
        return M_PROPERTY_UNAVAILABLE;
    return m_property_float_ro(prop, action, arg, mpctx->sh_video->fps);
}

/// Video aspect (RO)
static int mp_property_aspect(m_option_t *prop, int action, void *arg,
                              MPContext *mpctx)
{
    struct sh_video *sh_video = mpctx->sh_video;
    if (!mpctx->sh_video)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_SET: {
        mpctx->opts->movie_aspect = *(float *)arg;
        reinit_video_filters(mpctx);
        mp_force_video_refresh(mpctx);
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_GET: {
        float aspect = -1;
        struct mp_image_params *params = sh_video->vf_input;
        if (params && params->d_w && params->d_h) {
            aspect = (float)params->d_w / params->d_h;
        } else if (sh_video->disp_w && sh_video->disp_h) {
            aspect = (float)sh_video->disp_w / sh_video->disp_h;
        }
        if (aspect <= 0)
            return M_PROPERTY_UNAVAILABLE;
        *(float *)arg = aspect;
        return M_PROPERTY_OK;
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

// For OSD and subtitle related properties using the generic option bridge.
// - Fail as unavailable if no video is active
// - Trigger OSD state update when property is set
static int property_osd_helper(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    if (!mpctx->video_out)
        return M_PROPERTY_UNAVAILABLE;
    if (action == M_PROPERTY_SET)
        osd_changed_all(mpctx->osd);
    return mp_property_generic_option(prop, action, arg, mpctx);
}

/// Selected subtitles (RW)
static int mp_property_sub(m_option_t *prop, int action, void *arg,
                           MPContext *mpctx)
{
    return property_switch_track(prop, action, arg, mpctx, STREAM_SUB);
}

/// Subtitle delay (RW)
static int mp_property_sub_delay(m_option_t *prop, int action, void *arg,
                                 MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    if (!mpctx->video_out)
        return M_PROPERTY_UNAVAILABLE;
    switch (action) {
    case M_PROPERTY_PRINT:
        *(char **)arg = format_delay(opts->sub_delay);
        return M_PROPERTY_OK;
    }
    return property_osd_helper(prop, action, arg, mpctx);
}

static int mp_property_sub_pos(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    if (!mpctx->video_out)
        return M_PROPERTY_UNAVAILABLE;
    if (action == M_PROPERTY_PRINT) {
        *(char **)arg = talloc_asprintf(NULL, "%d/100", opts->sub_pos);
        return M_PROPERTY_OK;
    }
    return property_osd_helper(prop, action, arg, mpctx);
}

#ifdef CONFIG_TV

static tvi_handle_t *get_tvh(struct MPContext *mpctx)
{
    if (!(mpctx->master_demuxer && mpctx->master_demuxer->type == DEMUXER_TYPE_TV))
        return NULL;
    return mpctx->master_demuxer->priv;
}

/// TV color settings (RW)
static int mp_property_tv_color(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    tvi_handle_t *tvh = get_tvh(mpctx);
    if (!tvh)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_SET:
        return tv_set_color_options(tvh, prop->offset, *(int *) arg);
    case M_PROPERTY_GET:
        return tv_get_color_options(tvh, prop->offset, arg);
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

#endif

static int mp_property_playlist_pos(m_option_t *prop, int action, void *arg,
                                    MPContext *mpctx)
{
    struct playlist *pl = mpctx->playlist;
    if (!pl->first)
        return M_PROPERTY_UNAVAILABLE;

    switch (action) {
    case M_PROPERTY_GET: {
        int pos = playlist_entry_to_index(pl, pl->current);
        if (pos < 0)
            return M_PROPERTY_UNAVAILABLE;
        *(int *)arg = pos;
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_SET: {
        struct playlist_entry *e = playlist_entry_from_index(pl, *(int *)arg);
        if (!e)
            return M_PROPERTY_ERROR;
        mp_set_playlist_entry(mpctx, e);
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_GET_TYPE: {
        struct m_option opt = {
            .name = prop->name,
            .type = CONF_TYPE_INT,
            .flags = CONF_RANGE,
            .min = 0,
            .max = playlist_entry_count(pl) - 1,
        };
        *(struct m_option *)arg = opt;
        return M_PROPERTY_OK;
    }
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static int mp_property_playlist_count(m_option_t *prop, int action, void *arg,
                                      MPContext *mpctx)
{
    if (action == M_PROPERTY_GET) {
        *(int *)arg = playlist_entry_count(mpctx->playlist);
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static int mp_property_playlist(m_option_t *prop, int action, void *arg,
                                MPContext *mpctx)
{
    if (action == M_PROPERTY_GET) {
        char *res = talloc_strdup(NULL, "");

        for (struct playlist_entry *e = mpctx->playlist->first; e; e = e->next)
        {
            if (mpctx->playlist->current == e) {
                res = talloc_asprintf_append(res, "> %s <\n", e->filename);
            } else {
                res = talloc_asprintf_append(res, "%s\n", e->filename);
            }
        }

        *(char **)arg = res;
        return M_PROPERTY_OK;
    }
    return M_PROPERTY_NOT_IMPLEMENTED;
}

static char *print_obj_osd_list(struct m_obj_settings *list)
{
    char *res = NULL;
    for (int n = 0; list && list[n].name; n++) {
        res = talloc_asprintf_append(res, "%s [", list[n].name);
        for (int i = 0; list[n].attribs && list[n].attribs[i]; i += 2) {
            res = talloc_asprintf_append(res, "%s%s=%s", i > 0 ? " " : "",
                                         list[n].attribs[i],
                                         list[n].attribs[i + 1]);
        }
        res = talloc_asprintf_append(res, "]\n");
    }
    if (!res)
        res = talloc_strdup(NULL, "(empty)");
    return res;
}

static int property_filter(m_option_t *prop, int action, void *arg,
                           MPContext *mpctx, enum stream_type mt)
{
    switch (action) {
    case M_PROPERTY_PRINT: {
        struct m_config_option *opt = m_config_get_co(mpctx->mconfig,
                                                      bstr0(prop->name));
        *(char **)arg = print_obj_osd_list(*(struct m_obj_settings **)opt->data);
        return M_PROPERTY_OK;
    }
    case M_PROPERTY_SET:
        return set_filters(mpctx, mt, *(struct m_obj_settings **)arg) >= 0
            ? M_PROPERTY_OK : M_PROPERTY_ERROR;
    }
    return mp_property_generic_option(prop, action, arg, mpctx);
}

static int mp_property_vf(m_option_t *prop, int action, void *arg,
                          MPContext *mpctx)
{
    return property_filter(prop, action, arg, mpctx, STREAM_VIDEO);
}

static int mp_property_af(m_option_t *prop, int action, void *arg,
                          MPContext *mpctx)
{
    return property_filter(prop, action, arg, mpctx, STREAM_AUDIO);
}

static int mp_property_alias(m_option_t *prop, int action, void *arg,
                             MPContext *mpctx)
{
    const char *real_property = prop->priv;
    int r = mp_property_do(real_property, action, arg, mpctx);
    if (action == M_PROPERTY_GET_TYPE && r >= 0) {
        // Fix the property name
        struct m_option *type = arg;
        type->name = prop->name;
    }
    return r;
}

static int mp_property_options(m_option_t *prop, int action, void *arg,
                               MPContext *mpctx)
{
    if (action != M_PROPERTY_KEY_ACTION)
        return M_PROPERTY_NOT_IMPLEMENTED;

    struct m_property_action_arg *ka = arg;

    struct m_config_option *opt = m_config_get_co(mpctx->mconfig,
                                                  bstr0(ka->key));
    if (!opt)
        return M_PROPERTY_UNKNOWN;
    if (!opt->data)
        return M_PROPERTY_UNAVAILABLE;

    switch (ka->action) {
    case M_PROPERTY_GET:
        m_option_copy(opt->opt, ka->arg, opt->data);
        return M_PROPERTY_OK;
    case M_PROPERTY_SET:
        if (!(mpctx->initialized_flags & INITIALIZED_PLAYBACK) &&
            !(opt->opt->flags & (M_OPT_PRE_PARSE | M_OPT_GLOBAL)))
        {
            m_option_copy(opt->opt, opt->data, ka->arg);
            return M_PROPERTY_OK;
        }
        return M_PROPERTY_ERROR;
    case M_PROPERTY_GET_TYPE:
        *(struct m_option *)ka->arg = *opt->opt;
        return M_PROPERTY_OK;
    }

    return M_PROPERTY_NOT_IMPLEMENTED;
}

// Use option-to-property-bridge. (The property and option have the same names.)
#define M_OPTION_PROPERTY(name) \
    {(name), mp_property_generic_option, &m_option_type_dummy, 0, 0, 0, (name)}

// OPTION_PROPERTY(), but with a custom property handler. The custom handler
// must let unknown operations fall back to mp_property_generic_option().
#define M_OPTION_PROPERTY_CUSTOM(name, handler) \
    {(name), (handler), &m_option_type_dummy, 0, 0, 0, (name)}
#define M_OPTION_PROPERTY_CUSTOM_(name, handler, ...) \
    {(name), (handler), &m_option_type_dummy, 0, 0, 0, (name), __VA_ARGS__}

// Redirect a property name to another
#define M_PROPERTY_ALIAS(name, real_property) \
    {(name), mp_property_alias, &m_option_type_dummy, 0, 0, 0, (real_property)}

/// All properties available in MPlayer.
/** \ingroup Properties
 */
static const m_option_t mp_properties[] = {
    // General
    M_OPTION_PROPERTY("osd-level"),
    M_OPTION_PROPERTY_CUSTOM("osd-scale", property_osd_helper),
    M_OPTION_PROPERTY("loop"),
    M_OPTION_PROPERTY_CUSTOM("speed", mp_property_playback_speed),
    { "filename", mp_property_filename, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "path", mp_property_path, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "media-title", mp_property_media_title, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "stream-path", mp_property_stream_path, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    M_OPTION_PROPERTY_CUSTOM("stream-capture", mp_property_stream_capture),
    { "demuxer", mp_property_demuxer, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "stream-pos", mp_property_stream_pos, CONF_TYPE_INT64,
      M_OPT_MIN, 0, 0, NULL },
    { "stream-start", mp_property_stream_start, CONF_TYPE_INT64,
      M_OPT_MIN, 0, 0, NULL },
    { "stream-end", mp_property_stream_end, CONF_TYPE_INT64,
      M_OPT_MIN, 0, 0, NULL },
    { "stream-length", mp_property_stream_length, CONF_TYPE_INT64,
      M_OPT_MIN, 0, 0, NULL },
    { "stream-time-pos", mp_property_stream_time_pos, CONF_TYPE_TIME,
      M_OPT_MIN, 0, 0, NULL },
    { "length", mp_property_length, CONF_TYPE_TIME,
      M_OPT_MIN, 0, 0, NULL },
    { "avsync", mp_property_avsync, CONF_TYPE_DOUBLE },
    { "percent-pos", mp_property_percent_pos, CONF_TYPE_DOUBLE,
      M_OPT_RANGE, 0, 100, NULL },
    { "time-pos", mp_property_time_pos, CONF_TYPE_TIME,
      M_OPT_MIN, 0, 0, NULL },
    { "time-remaining", mp_property_remaining, CONF_TYPE_TIME },
    { "chapter", mp_property_chapter, CONF_TYPE_INT,
      M_OPT_MIN, -1, 0, NULL },
    M_OPTION_PROPERTY_CUSTOM("edition", mp_property_edition),
    M_OPTION_PROPERTY_CUSTOM("quvi-format", mp_property_quvi_format),
    { "titles", mp_property_titles, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "chapters", mp_property_chapters, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "editions", mp_property_editions, CONF_TYPE_INT },
    { "angle", mp_property_angle, &m_option_type_dummy },
    { "metadata", mp_property_metadata, CONF_TYPE_STRING_LIST },
    { "chapter-metadata", mp_property_chapter_metadata, CONF_TYPE_STRING_LIST },
    M_OPTION_PROPERTY_CUSTOM("pause", mp_property_pause),
    { "cache", mp_property_cache, CONF_TYPE_INT },
    M_OPTION_PROPERTY("pts-association-mode"),
    M_OPTION_PROPERTY("hr-seek"),
    { "clock", mp_property_clock, CONF_TYPE_STRING,
      0, 0, 0, NULL },

    { "chapter-list", mp_property_list_chapters, CONF_TYPE_STRING },
    { "track-list", property_list_tracks, CONF_TYPE_STRING },

    { "playlist", mp_property_playlist, CONF_TYPE_STRING },
    { "playlist-pos", mp_property_playlist_pos, CONF_TYPE_INT },
    { "playlist-count", mp_property_playlist_count, CONF_TYPE_INT },

    // Audio
    { "volume", mp_property_volume, CONF_TYPE_FLOAT,
      M_OPT_RANGE, 0, 100, NULL },
    { "mute", mp_property_mute, CONF_TYPE_FLAG,
      M_OPT_RANGE, 0, 1, NULL },
    M_OPTION_PROPERTY_CUSTOM("audio-delay", mp_property_audio_delay),
    { "audio-format", mp_property_audio_format, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "audio-codec", mp_property_audio_codec, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "audio-bitrate", mp_property_audio_bitrate, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "samplerate", mp_property_samplerate, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "channels", mp_property_channels, CONF_TYPE_INT,
      0, 0, 0, NULL },
    M_OPTION_PROPERTY_CUSTOM("aid", mp_property_audio),
    { "balance", mp_property_balance, CONF_TYPE_FLOAT,
      M_OPT_RANGE, -1, 1, NULL },
    M_OPTION_PROPERTY_CUSTOM("volume-restore-data", mp_property_volrestore),

    // Video
    M_OPTION_PROPERTY_CUSTOM("fullscreen", mp_property_fullscreen),
    { "deinterlace", mp_property_deinterlace, CONF_TYPE_FLAG,
      M_OPT_RANGE, 0, 1, NULL },
    M_OPTION_PROPERTY_CUSTOM("colormatrix", mp_property_colormatrix),
    M_OPTION_PROPERTY_CUSTOM("colormatrix-input-range",
                             mp_property_colormatrix_input_range),
    M_OPTION_PROPERTY_CUSTOM("colormatrix-output-range",
                             mp_property_colormatrix_output_range),
    M_OPTION_PROPERTY_CUSTOM("ontop", mp_property_ontop),
    M_OPTION_PROPERTY_CUSTOM("border", mp_property_border),
    M_OPTION_PROPERTY_CUSTOM("framedrop", mp_property_framedrop),
    M_OPTION_PROPERTY_CUSTOM("gamma", mp_property_video_color),
    M_OPTION_PROPERTY_CUSTOM("brightness", mp_property_video_color),
    M_OPTION_PROPERTY_CUSTOM("contrast", mp_property_video_color),
    M_OPTION_PROPERTY_CUSTOM("saturation", mp_property_video_color),
    M_OPTION_PROPERTY_CUSTOM("hue", mp_property_video_color),
    M_OPTION_PROPERTY_CUSTOM("panscan", panscan_property_helper),
    M_OPTION_PROPERTY_CUSTOM("video-zoom", panscan_property_helper),
    M_OPTION_PROPERTY_CUSTOM("video-align-x", panscan_property_helper),
    M_OPTION_PROPERTY_CUSTOM("video-align-y", panscan_property_helper),
    M_OPTION_PROPERTY_CUSTOM("video-pan-x", panscan_property_helper),
    M_OPTION_PROPERTY_CUSTOM("video-pan-y", panscan_property_helper),
    M_OPTION_PROPERTY_CUSTOM("video-unscaled", panscan_property_helper),
    { "video-format", mp_property_video_format, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "video-codec", mp_property_video_codec, CONF_TYPE_STRING,
      0, 0, 0, NULL },
    { "video-bitrate", mp_property_video_bitrate, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "width", mp_property_width, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "height", mp_property_height, CONF_TYPE_INT,
      0, 0, 0, NULL },
    { "dwidth", mp_property_dwidth, CONF_TYPE_INT },
    { "dheight", mp_property_dheight, CONF_TYPE_INT },
    { "fps", mp_property_fps, CONF_TYPE_FLOAT,
      0, 0, 0, NULL },
    { "aspect", mp_property_aspect, CONF_TYPE_FLOAT,
      CONF_RANGE, -1, 10, NULL },
    M_OPTION_PROPERTY_CUSTOM("vid", mp_property_video),
    { "program", mp_property_program, CONF_TYPE_INT,
      CONF_RANGE, -1, 65535, NULL },

    { "osd-width", mp_property_osd_w, CONF_TYPE_INT },
    { "osd-height", mp_property_osd_h, CONF_TYPE_INT },
    { "osd-par", mp_property_osd_par, CONF_TYPE_DOUBLE },

    // Subs
    M_OPTION_PROPERTY_CUSTOM("sid", mp_property_sub),
    M_OPTION_PROPERTY_CUSTOM("sub-delay", mp_property_sub_delay),
    M_OPTION_PROPERTY_CUSTOM("sub-pos", mp_property_sub_pos),
    M_OPTION_PROPERTY_CUSTOM("sub-visibility", property_osd_helper),
    M_OPTION_PROPERTY_CUSTOM("sub-forced-only", property_osd_helper),
    M_OPTION_PROPERTY_CUSTOM("sub-scale", property_osd_helper),
#ifdef CONFIG_ASS
    M_OPTION_PROPERTY_CUSTOM("ass-use-margins", property_osd_helper),
    M_OPTION_PROPERTY_CUSTOM("ass-vsfilter-aspect-compat", property_osd_helper),
    M_OPTION_PROPERTY_CUSTOM("ass-style-override", property_osd_helper),
#endif

    M_OPTION_PROPERTY_CUSTOM("vf*", mp_property_vf),
    M_OPTION_PROPERTY_CUSTOM("af*", mp_property_af),

#ifdef CONFIG_TV
    { "tv-brightness", mp_property_tv_color, CONF_TYPE_INT,
      M_OPT_RANGE, -100, 100, .offset = TV_COLOR_BRIGHTNESS },
    { "tv-contrast", mp_property_tv_color, CONF_TYPE_INT,
      M_OPT_RANGE, -100, 100, .offset = TV_COLOR_CONTRAST },
    { "tv-saturation", mp_property_tv_color, CONF_TYPE_INT,
      M_OPT_RANGE, -100, 100, .offset = TV_COLOR_SATURATION },
    { "tv-hue", mp_property_tv_color, CONF_TYPE_INT,
      M_OPT_RANGE, -100, 100, .offset = TV_COLOR_HUE },
#endif

    M_PROPERTY_ALIAS("video", "vid"),
    M_PROPERTY_ALIAS("audio", "aid"),
    M_PROPERTY_ALIAS("sub", "sid"),

    { "options", mp_property_options, &m_option_type_dummy },

    {0},
};

const struct m_option *mp_get_property_list(void)
{
    return mp_properties;
}

int mp_property_do(const char *name, int action, void *val,
                   struct MPContext *ctx)
{
    return m_property_do(mp_properties, name, action, val, ctx);
}

char *mp_property_expand_string(struct MPContext *mpctx, const char *str)
{
    return m_properties_expand_string(mp_properties, str, mpctx);
}

void property_print_help(void)
{
    m_properties_print_help_list(mp_properties);
}


/* List of default ways to show a property on OSD.
 *
 * If osd_progbar is set, a bar showing the current position between min/max
 * values of the property is shown. In this case osd_msg is only used for
 * terminal output if there is no video; it'll be a label shown together with
 * percentage.
 */
static struct property_osd_display {
    // property name
    const char *name;
    // name used on OSD
    const char *osd_name;
    // progressbar type
    int osd_progbar;
    // osd msg id if it must be shared
    int osd_id;
    // Needs special ways to display the new value (seeks are delayed)
    int seek_msg, seek_bar;
    // Free-form message (if NULL, osd_name or the property name is used)
    const char *msg;
    // Extra free-from message (just for volume)
    const char *extra_msg;
} property_osd_display[] = {
    // general
    { "loop", _("Loop") },
    { "chapter", .seek_msg = OSD_SEEK_INFO_CHAPTER_TEXT,
                 .seek_bar = OSD_SEEK_INFO_BAR },
    { "edition", .seek_msg = OSD_SEEK_INFO_EDITION },
    { "pts-association-mode", "PTS association mode" },
    { "hr-seek", "hr-seek" },
    { "speed", _("Speed") },
    { "clock", _("Clock") },
    // audio
    { "volume", _("Volume"),
      .extra_msg = "${?mute==yes:(Muted)}", .osd_progbar = OSD_VOLUME },
    { "mute", _("Mute") },
    { "audio-delay", _("A-V delay") },
    { "audio", _("Audio") },
    { "balance", _("Balance"), .osd_progbar = OSD_BALANCE },
    // video
    { "panscan", _("Panscan"), .osd_progbar = OSD_PANSCAN },
    { "ontop", _("Stay on top") },
    { "border", _("Border") },
    { "framedrop", _("Framedrop") },
    { "deinterlace", _("Deinterlace") },
    { "colormatrix", _("YUV colormatrix") },
    { "colormatrix-input-range", _("YUV input range") },
    { "colormatrix-output-range", _("RGB output range") },
    { "gamma", _("Gamma"), .osd_progbar = OSD_BRIGHTNESS },
    { "brightness", _("Brightness"), .osd_progbar = OSD_BRIGHTNESS },
    { "contrast", _("Contrast"), .osd_progbar = OSD_CONTRAST },
    { "saturation", _("Saturation"), .osd_progbar = OSD_SATURATION },
    { "hue", _("Hue"), .osd_progbar = OSD_HUE },
    { "angle", _("Angle") },
    // subs
    { "sub", _("Subtitles") },
    { "sub-pos", _("Sub position") },
    { "sub-delay", _("Sub delay"), .osd_id = OSD_MSG_SUB_DELAY },
    { "sub-visibility", _("Subtitles") },
    { "sub-forced-only", _("Forced sub only") },
    { "sub-scale", _("Sub Scale")},
    { "ass-vsfilter-aspect-compat", _("Subtitle VSFilter aspect compat")},
    { "ass-style-override", _("ASS subtitle style override")},
    { "vf*", _("Video filters"), .msg = "Video filters:\n${vf}"},
    { "af*", _("Audio filters"), .msg = "Audio filters:\n${af}"},
#ifdef CONFIG_TV
    { "tv-brightness", _("Brightness"), .osd_progbar = OSD_BRIGHTNESS },
    { "tv-hue", _("Hue"), .osd_progbar = OSD_HUE},
    { "tv-saturation", _("Saturation"), .osd_progbar = OSD_SATURATION },
    { "tv-contrast", _("Contrast"), .osd_progbar = OSD_CONTRAST },
#endif
    {0}
};

static void show_property_osd(MPContext *mpctx, const char *pname,
                              enum mp_on_osd osd_mode)
{
    struct MPOpts *opts = mpctx->opts;
    struct m_option prop = {0};
    struct property_osd_display *p;

    if (mp_property_do(pname, M_PROPERTY_GET_TYPE, &prop, mpctx) <= 0)
        return;

    int osd_progbar = 0;
    const char *osd_name = NULL;
    const char *msg = NULL;
    const char *extra_msg = NULL;

    // look for the command
    for (p = property_osd_display; p->name; p++) {
        if (!strcmp(p->name, prop.name)) {
            osd_progbar = p->seek_bar ? 1 : p->osd_progbar;
            osd_name = p->seek_msg ? "" : mp_gtext(p->osd_name);
            break;
        }
    }
    if (!p->name)
        p = NULL;

    if (p) {
        msg = p->msg;
        extra_msg = p->extra_msg;
    }

    if (osd_mode != MP_ON_OSD_AUTO) {
        osd_name = osd_name ? osd_name : prop.name;
        if (!(osd_mode & MP_ON_OSD_MSG)) {
            osd_name = NULL;
            msg = NULL;
            extra_msg = NULL;
        }
        osd_progbar = osd_progbar ? osd_progbar : ' ';
        if (!(osd_mode & MP_ON_OSD_BAR))
            osd_progbar = 0;
    }

    if (p && (p->seek_msg || p->seek_bar)) {
        mpctx->add_osd_seek_info |=
            (osd_name ? p->seek_msg : 0) | (osd_progbar ? p->seek_bar : 0);
        return;
    }

    void *tmp = talloc_new(NULL);

    if (!msg && osd_name)
        msg = talloc_asprintf(tmp, "%s: ${%s}", osd_name, prop.name);

    if (osd_progbar && (prop.flags & CONF_RANGE) == CONF_RANGE) {
        bool ok = false;
        if (prop.type == CONF_TYPE_INT) {
            int i;
            ok = mp_property_do(prop.name, M_PROPERTY_GET, &i, mpctx) > 0;
            if (ok)
                set_osd_bar(mpctx, osd_progbar, osd_name, prop.min, prop.max, i);
        } else if (prop.type == CONF_TYPE_FLOAT) {
            float f;
            ok = mp_property_do(prop.name, M_PROPERTY_GET, &f, mpctx) > 0;
            if (ok)
                set_osd_bar(mpctx, osd_progbar, osd_name, prop.min, prop.max, f);
        }
        if (ok && osd_mode == MP_ON_OSD_AUTO && opts->osd_bar_visible)
            msg = NULL;
    }

    char *osd_msg = NULL;
    if (msg)
        osd_msg = talloc_steal(tmp, mp_property_expand_string(mpctx, msg));
    if (extra_msg) {
        char *t = talloc_steal(tmp, mp_property_expand_string(mpctx, extra_msg));
        osd_msg = talloc_asprintf(tmp, "%s%s%s", osd_msg ? osd_msg : "",
                                  osd_msg && osd_msg[0] ? " " : "", t);
    }

    if (osd_msg && osd_msg[0]) {
        int osd_id = 0;
        if (p) {
            int index = p - property_osd_display;
            osd_id = p->osd_id ? p->osd_id : OSD_MSG_PROPERTY + index;
        }
        set_osd_tmsg(mpctx, osd_id, 1, opts->osd_duration, "%s", osd_msg);
    }

    talloc_free(tmp);
}

static const char *property_error_string(int error_value)
{
    switch (error_value) {
    case M_PROPERTY_ERROR:
        return "ERROR";
    case M_PROPERTY_UNAVAILABLE:
        return "PROPERTY_UNAVAILABLE";
    case M_PROPERTY_NOT_IMPLEMENTED:
        return "NOT_IMPLEMENTED";
    case M_PROPERTY_UNKNOWN:
        return "PROPERTY_UNKNOWN";
    }
    return "UNKNOWN";
}
