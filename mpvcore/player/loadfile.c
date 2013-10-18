
static void set_demux_field(struct MPContext *mpctx, enum stream_type type,
                            struct sh_stream *s)
{
    mpctx->sh[type] = s;
    // redundant fields for convenience access
    switch(type) {
        case STREAM_VIDEO: mpctx->sh_video = s ? s->video : NULL; break;
        case STREAM_AUDIO: mpctx->sh_audio = s ? s->audio : NULL; break;
        case STREAM_SUB: mpctx->sh_sub = s ? s->sub : NULL; break;
    }
}

static void init_demux_stream(struct MPContext *mpctx, enum stream_type type)
{
    struct track *track = mpctx->current_track[type];
    set_demux_field(mpctx, type, track ? track->stream : NULL);
    struct sh_stream *stream = mpctx->sh[type];
    if (stream) {
        demuxer_switch_track(stream->demuxer, type, stream);
        if (track->is_external) {
            double pts = get_main_demux_pts(mpctx);
            demux_seek(stream->demuxer, pts, SEEK_ABSOLUTE);
        }
    }
}

static void cleanup_demux_stream(struct MPContext *mpctx, enum stream_type type)
{
    struct sh_stream *stream = mpctx->sh[type];
    if (stream)
        demuxer_switch_track(stream->demuxer, type, NULL);
    set_demux_field(mpctx, type, NULL);
}

// Switch the demuxers to current track selection. This is possibly important
// for intialization: if something reads packets from the demuxer (like at least
// reinit_audio_chain does, or when seeking), packets from the other streams
// should be queued instead of discarded. So all streams should be enabled
// before the first initialization function is called.
static void preselect_demux_streams(struct MPContext *mpctx)
{
    // Disable all streams, just to be sure no unwanted streams are selected.
    for (int n = 0; n < mpctx->num_sources; n++) {
        for (int type = 0; type < STREAM_TYPE_COUNT; type++) {
            struct track *track = mpctx->current_track[type];
            if (!(track && track->demuxer == mpctx->sources[n] &&
                  demuxer_stream_is_selected(track->demuxer, track->stream)))
                demuxer_switch_track(mpctx->sources[n], type, NULL);
        }
    }

    for (int type = 0; type < STREAM_TYPE_COUNT; type++) {
        struct track *track = mpctx->current_track[type];
        if (track && track->stream)
            demuxer_switch_track(track->stream->demuxer, type, track->stream);
    }
}


void uninit_player(struct MPContext *mpctx, unsigned int mask)
{
    mask &= mpctx->initialized_flags;

    MP_DBG(mpctx, "\n*** uninit(0x%X)\n", mask);

    if (mask & INITIALIZED_ACODEC) {
        mpctx->initialized_flags &= ~INITIALIZED_ACODEC;
        mixer_uninit_audio(mpctx->mixer);
        if (mpctx->sh_audio)
            uninit_audio(mpctx->sh_audio);
        cleanup_demux_stream(mpctx, STREAM_AUDIO);
    }

    if (mask & INITIALIZED_SUB) {
        mpctx->initialized_flags &= ~INITIALIZED_SUB;
        if (mpctx->sh_sub)
            sub_reset(mpctx->sh_sub->dec_sub);
        cleanup_demux_stream(mpctx, STREAM_SUB);
        mpctx->osd->dec_sub = NULL;
        reset_subtitles(mpctx);
    }

    if (mask & INITIALIZED_VCODEC) {
        mpctx->initialized_flags &= ~INITIALIZED_VCODEC;
        if (mpctx->sh_video)
            uninit_video(mpctx->sh_video);
        cleanup_demux_stream(mpctx, STREAM_VIDEO);
        mpctx->sync_audio_to_video = false;
    }

    if (mask & INITIALIZED_DEMUXER) {
        mpctx->initialized_flags &= ~INITIALIZED_DEMUXER;
        for (int i = 0; i < mpctx->num_tracks; i++) {
            talloc_free(mpctx->tracks[i]);
        }
        mpctx->num_tracks = 0;
        for (int t = 0; t < STREAM_TYPE_COUNT; t++)
            mpctx->current_track[t] = NULL;
        assert(!mpctx->sh_video && !mpctx->sh_audio && !mpctx->sh_sub);
        mpctx->master_demuxer = NULL;
        for (int i = 0; i < mpctx->num_sources; i++) {
            uninit_subs(mpctx->sources[i]);
            struct demuxer *demuxer = mpctx->sources[i];
            if (demuxer->stream != mpctx->stream)
                free_stream(demuxer->stream);
            free_demuxer(demuxer);
        }
        talloc_free(mpctx->sources);
        mpctx->sources = NULL;
        mpctx->demuxer = NULL;
        mpctx->num_sources = 0;
        talloc_free(mpctx->timeline);
        mpctx->timeline = NULL;
        mpctx->num_timeline_parts = 0;
        talloc_free(mpctx->chapters);
        mpctx->chapters = NULL;
        mpctx->num_chapters = 0;
        mpctx->video_offset = 0;
    }

    // kill the cache process:
    if (mask & INITIALIZED_STREAM) {
        mpctx->initialized_flags &= ~INITIALIZED_STREAM;
        if (mpctx->stream)
            free_stream(mpctx->stream);
        mpctx->stream = NULL;
    }

    if (mask & INITIALIZED_VO) {
        mpctx->initialized_flags &= ~INITIALIZED_VO;
        vo_destroy(mpctx->video_out);
        mpctx->video_out = NULL;
    }

    // Must be after libvo uninit, as few vo drivers (svgalib) have tty code.
    if (mask & INITIALIZED_GETCH2) {
        mpctx->initialized_flags &= ~INITIALIZED_GETCH2;
        MP_DBG(mpctx, "\n[[[uninit getch2]]]\n");
        // restore terminal:
        getch2_disable();
    }

    if (mask & INITIALIZED_AO) {
        mpctx->initialized_flags &= ~INITIALIZED_AO;
        if (mpctx->ao)
            ao_uninit(mpctx->ao, mpctx->stop_play != AT_END_OF_FILE);
        mpctx->ao = NULL;
    }

    if (mask & INITIALIZED_PLAYBACK)
        mpctx->initialized_flags &= ~INITIALIZED_PLAYBACK;
}

static void mk_config_dir(char *subdir)
{
    void *tmp = talloc_new(NULL);
    char *confdir = talloc_steal(tmp, mp_find_user_config_file(""));
    if (confdir) {
        if (subdir)
            confdir = mp_path_join(tmp, bstr0(confdir), bstr0(subdir));
        mkdir(confdir, 0777);
    }
    talloc_free(tmp);
}

static int cfg_include(struct m_config *conf, char *filename, int flags)
{
    return m_config_parse_config_file(conf, filename, flags);
}

#define DEF_CONFIG "# Write your default config options here!\n\n\n"

static bool parse_cfgfiles(struct MPContext *mpctx, m_config_t *conf)
{
    struct MPOpts *opts = mpctx->opts;
    char *conffile;
    int conffile_fd;
    if (!opts->load_config)
        return true;
    if (!m_config_parse_config_file(conf, MPLAYER_CONFDIR "/mpv.conf", 0) < 0)
        return false;
    mk_config_dir(NULL);
    if ((conffile = mp_find_user_config_file("config")) == NULL)
        MP_ERR(mpctx, "mp_find_user_config_file(\"config\") problem\n");
    else {
        if ((conffile_fd = open(conffile, O_CREAT | O_EXCL | O_WRONLY,
                    0666)) != -1) {
            MP_INFO(mpctx, "Creating config file: %s\n", conffile);
            write(conffile_fd, DEF_CONFIG, sizeof(DEF_CONFIG) - 1);
            close(conffile_fd);
        }
        if (m_config_parse_config_file(conf, conffile, 0) < 0)
            return false;
        talloc_free(conffile);
    }
    return true;
}

#define PROFILE_CFG_PROTOCOL "protocol."

static void load_per_protocol_config(m_config_t *conf, const char * const file)
{
    char *str;
    char protocol[strlen(PROFILE_CFG_PROTOCOL) + strlen(file) + 1];
    m_profile_t *p;

    /* does filename actually uses a protocol ? */
    if (!mp_is_url(bstr0(file)))
        return;
    str = strstr(file, "://");
    if (!str)
        return;

    sprintf(protocol, "%s%s", PROFILE_CFG_PROTOCOL, file);
    protocol[strlen(PROFILE_CFG_PROTOCOL) + strlen(file) - strlen(str)] = '\0';
    p = m_config_get_profile0(conf, protocol);
    if (p) {
        mp_tmsg(MSGT_CPLAYER, MSGL_INFO,
                "Loading protocol-related profile '%s'\n", protocol);
        m_config_set_profile(conf, p, M_SETOPT_BACKUP);
    }
}

#define PROFILE_CFG_EXTENSION "extension."

static void load_per_extension_config(m_config_t *conf, const char * const file)
{
    char *str;
    char extension[strlen(PROFILE_CFG_EXTENSION) + 8];
    m_profile_t *p;

    /* does filename actually have an extension ? */
    str = strrchr(file, '.');
    if (!str)
        return;

    sprintf(extension, PROFILE_CFG_EXTENSION);
    strncat(extension, ++str, 7);
    p = m_config_get_profile0(conf, extension);
    if (p) {
        mp_tmsg(MSGT_CPLAYER, MSGL_INFO,
                "Loading extension-related profile '%s'\n", extension);
        m_config_set_profile(conf, p, M_SETOPT_BACKUP);
    }
}

#define PROFILE_CFG_VO "vo."
#define PROFILE_CFG_AO "ao."

static void load_per_output_config(m_config_t *conf, char *cfg, char *out)
{
    char profile[strlen(cfg) + strlen(out) + 1];
    m_profile_t *p;

    if (!out && !out[0])
        return;

    sprintf(profile, "%s%s", cfg, out);
    p = m_config_get_profile0(conf, profile);
    if (p) {
        mp_tmsg(MSGT_CPLAYER, MSGL_INFO,
                "Loading extension-related profile '%s'\n", profile);
        m_config_set_profile(conf, p, M_SETOPT_BACKUP);
    }
}

/**
 * Tries to load a config file (in file local mode)
 * @return 0 if file was not found, 1 otherwise
 */
static int try_load_config(m_config_t *conf, const char *file, bool local)
{
    if (!mp_path_exists(file))
        return 0;
    mp_tmsg(MSGT_CPLAYER, MSGL_INFO, "Loading config '%s'\n", file);
    m_config_parse_config_file(conf, file, local ? M_SETOPT_BACKUP : 0);
    return 1;
}

static void load_per_file_config(m_config_t *conf, const char * const file,
                                 bool search_file_dir)
{
    char *confpath;
    char cfg[MP_PATH_MAX];
    const char *name;

    if (strlen(file) > MP_PATH_MAX - 14) {
        mp_msg(MSGT_CPLAYER, MSGL_WARN, "Filename is too long, "
               "can not load file or directory specific config files\n");
        return;
    }
    sprintf(cfg, "%s.conf", file);

    name = mp_basename(cfg);
    if (search_file_dir) {
        char dircfg[MP_PATH_MAX];
        strcpy(dircfg, cfg);
        strcpy(dircfg + (name - cfg), "mpv.conf");
        try_load_config(conf, dircfg, true);

        if (try_load_config(conf, cfg, true))
            return;
    }

    if ((confpath = mp_find_user_config_file(name)) != NULL) {
        try_load_config(conf, confpath, true);

        talloc_free(confpath);
    }
}

#define MP_WATCH_LATER_CONF "watch_later"

static char *get_playback_resume_config_filename(const char *fname,
                                                 struct MPOpts *opts)
{
    char *res = NULL;
    void *tmp = talloc_new(NULL);
    const char *realpath = fname;
    bstr bfname = bstr0(fname);
    if (!mp_is_url(bfname)) {
        char *cwd = mp_getcwd(tmp);
        if (!cwd)
            goto exit;
        realpath = mp_path_join(tmp, bstr0(cwd), bstr0(fname));
    }
#ifdef CONFIG_DVDREAD
    if (bstr_startswith0(bfname, "dvd://"))
        realpath = talloc_asprintf(tmp, "%s - %s", realpath, dvd_device);
#endif
#ifdef CONFIG_LIBBLURAY
    if (bstr_startswith0(bfname, "br://") || bstr_startswith0(bfname, "bd://") ||
        bstr_startswith0(bfname, "bluray://"))
        realpath = talloc_asprintf(tmp, "%s - %s", realpath, bluray_device);
#endif
    uint8_t md5[16];
    av_md5_sum(md5, realpath, strlen(realpath));
    char *conf = talloc_strdup(tmp, "");
    for (int i = 0; i < 16; i++)
        conf = talloc_asprintf_append(conf, "%02X", md5[i]);

    conf = talloc_asprintf(tmp, "%s/%s", MP_WATCH_LATER_CONF, conf);

    res = mp_find_user_config_file(conf);

exit:
    talloc_free(tmp);
    return res;
}

static const char *backup_properties[] = {
    "osd-level",
    //"loop",
    "speed",
    "edition",
    "pause",
    "volume-restore-data",
    "audio-delay",
    //"balance",
    "fullscreen",
    "colormatrix",
    "colormatrix-input-range",
    "colormatrix-output-range",
    "ontop",
    "border",
    "gamma",
    "brightness",
    "contrast",
    "saturation",
    "hue",
    "deinterlace",
    "vf",
    "af",
    "panscan",
    "aid",
    "vid",
    "sid",
    "sub-delay",
    "sub-pos",
    "sub-visibility",
    "sub-scale",
    "ass-use-margins",
    "ass-vsfilter-aspect-compat",
    "ass-style-override",
    0
};

// Should follow what parser-cfg.c does/needs
static bool needs_config_quoting(const char *s)
{
    for (int i = 0; s && s[i]; i++) {
        unsigned char c = s[i];
        if (!isprint(c) || isspace(c) || c == '#' || c == '\'' || c == '"')
            return true;
    }
    return false;
}

void mp_write_watch_later_conf(struct MPContext *mpctx)
{
    void *tmp = talloc_new(NULL);
    char *filename = mpctx->filename;
    if (!filename)
        goto exit;

    double pos = get_current_time(mpctx);
    if (pos == MP_NOPTS_VALUE)
        goto exit;

    mk_config_dir(MP_WATCH_LATER_CONF);

    char *conffile = get_playback_resume_config_filename(mpctx->filename,
                                                         mpctx->opts);
    talloc_steal(tmp, conffile);
    if (!conffile)
        goto exit;

    MP_INFO(mpctx, "Saving state.\n");

    FILE *file = fopen(conffile, "wb");
    if (!file)
        goto exit;
    fprintf(file, "start=%f\n", pos);
    for (int i = 0; backup_properties[i]; i++) {
        const char *pname = backup_properties[i];
        char *val = NULL;
        int r = mp_property_do(pname, M_PROPERTY_GET_STRING, &val, mpctx);
        if (r == M_PROPERTY_OK) {
            if (needs_config_quoting(val)) {
                // e.g. '%6%STRING'
                fprintf(file, "%s=%%%d%%%s\n", pname, (int)strlen(val), val);
            } else {
                fprintf(file, "%s=%s\n", pname, val);
            }
        }
        talloc_free(val);
    }
    fclose(file);

exit:
    talloc_free(tmp);
}

static void load_playback_resume(m_config_t *conf, const char *file)
{
    char *fname = get_playback_resume_config_filename(file, conf->optstruct);
    if (fname && mp_path_exists(fname)) {
        // Never apply the saved start position to following files
        m_config_backup_opt(conf, "start");
        mp_msg(MSGT_CPLAYER, MSGL_INFO, "Resuming playback. This behavior can "
               "be disabled with --no-resume-playback.\n");
        try_load_config(conf, fname, false);
        unlink(fname);
    }
    talloc_free(fname);
}

// Returns the first file that has a resume config.
// Compared to hashing the playlist file or contents and managing separate
// resume file for them, this is simpler, and also has the nice property
// that appending to a playlist doesn't interfere with resuming (especially
// if the playlist comes from the command line).
struct playlist_entry *mp_resume_playlist(struct playlist *playlist,
                                          struct MPOpts *opts)
{
    if (!opts->position_resume)
        return NULL;
    for (struct playlist_entry *e = playlist->first; e; e = e->next) {
        char *conf = get_playback_resume_config_filename(e->filename, opts);
        bool exists = conf && mp_path_exists(conf);
        talloc_free(conf);
        if (exists)
            return e;
    }
    return NULL;
}

static void load_per_file_options(m_config_t *conf,
                                  struct playlist_param *params,
                                  int params_count)
{
    for (int n = 0; n < params_count; n++) {
        m_config_set_option_ext(conf, params[n].name, params[n].value,
                                M_SETOPT_BACKUP);
    }
}

/* When demux performs a blocking operation (network connection or
 * cache filling) if the operation fails we use this function to check
 * if it was interrupted by the user.
 * The function returns whether it was interrupted. */
static bool demux_was_interrupted(struct MPContext *mpctx)
{
    for (;;) {
        if (mpctx->stop_play != KEEP_PLAYING
            && mpctx->stop_play != AT_END_OF_FILE)
            return true;
        mp_cmd_t *cmd = mp_input_get_cmd(mpctx->input, 0, 0);
        if (!cmd)
            break;
        if (mp_input_is_abort_cmd(cmd->id))
            run_command(mpctx, cmd);
        mp_cmd_free(cmd);
    }
    return false;
}

static int find_new_tid(struct MPContext *mpctx, enum stream_type t)
{
    int new_id = 0;
    for (int i = 0; i < mpctx->num_tracks; i++) {
        struct track *track = mpctx->tracks[i];
        if (track->type == t)
            new_id = FFMAX(new_id, track->user_tid);
    }
    return new_id + 1;
}

// Map stream number (as used by libdvdread) to MPEG IDs (as used by demuxer).
static int map_id_from_demuxer(struct demuxer *d, enum stream_type type, int id)
{
    if (d->stream->uncached_type == STREAMTYPE_DVD && type == STREAM_SUB)
        id = id & 0x1F;
    return id;
}

static struct track *add_stream_track(struct MPContext *mpctx,
                                      struct sh_stream *stream,
                                      bool under_timeline)
{
    for (int i = 0; i < mpctx->num_tracks; i++) {
        struct track *track = mpctx->tracks[i];
        if (track->stream == stream)
            return track;
        // DVD subtitle track that was added later
        if (stream->type == STREAM_SUB && track->type == STREAM_SUB &&
            map_id_from_demuxer(stream->demuxer, stream->type,
                                stream->demuxer_id) == track->demuxer_id
            && !track->stream)
        {
            track->stream = stream;
            track->demuxer_id = stream->demuxer_id;
            // Initialize lazily selected track
            bool selected = track == mpctx->current_track[STREAM_SUB];
            demuxer_select_track(track->demuxer, stream, selected);
            if (selected)
                reinit_subs(mpctx);
            return track;
        }
    }

    struct track *track = talloc_ptrtype(NULL, track);
    *track = (struct track) {
        .type = stream->type,
        .user_tid = find_new_tid(mpctx, stream->type),
        .demuxer_id = stream->demuxer_id,
        .title = stream->title,
        .default_track = stream->default_track,
        .attached_picture = stream->attached_picture != NULL,
        .lang = stream->lang,
        .under_timeline = under_timeline,
        .demuxer = stream->demuxer,
        .stream = stream,
    };
    MP_TARRAY_APPEND(mpctx, mpctx->tracks, mpctx->num_tracks, track);

    if (stream->type == STREAM_SUB)
        track->preloaded = !!stream->sub->track;

    // Needed for DVD and Blu-ray.
    if (!track->lang) {
        struct stream_lang_req req = {
            .type = track->type,
            .id = map_id_from_demuxer(track->demuxer, track->type,
                                      track->demuxer_id)
        };
        stream_control(track->demuxer->stream, STREAM_CTRL_GET_LANG, &req);
        if (req.name[0])
            track->lang = talloc_strdup(track, req.name);
    }

    demuxer_select_track(track->demuxer, stream, false);

    mp_notify(mpctx, MP_EVENT_TRACKS_CHANGED, NULL);

    return track;
}

static void add_demuxer_tracks(struct MPContext *mpctx, struct demuxer *demuxer)
{
    for (int n = 0; n < demuxer->num_streams; n++)
        add_stream_track(mpctx, demuxer->streams[n], !!mpctx->timeline);
}

static void add_dvd_tracks(struct MPContext *mpctx)
{
#ifdef CONFIG_DVDREAD
    struct demuxer *demuxer = mpctx->demuxer;
    struct stream *stream = demuxer->stream;
    struct stream_dvd_info_req info;
    if (stream_control(stream, STREAM_CTRL_GET_DVD_INFO, &info) > 0) {
        for (int n = 0; n < info.num_subs; n++) {
            struct track *track = talloc_ptrtype(NULL, track);
            *track = (struct track) {
                .type = STREAM_SUB,
                .user_tid = find_new_tid(mpctx, STREAM_SUB),
                .demuxer_id = n,
                .demuxer = mpctx->demuxer,
            };
            MP_TARRAY_APPEND(mpctx, mpctx->tracks, mpctx->num_tracks, track);

            struct stream_lang_req req = {.type = STREAM_SUB, .id = n};
            stream_control(stream, STREAM_CTRL_GET_LANG, &req);
            track->lang = talloc_strdup(track, req.name);

            mp_notify(mpctx, MP_EVENT_TRACKS_CHANGED, NULL);
        }
    }
    demuxer_enable_autoselect(demuxer);
#endif
}

static char *track_layout_hash(struct MPContext *mpctx)
{
    char *h = talloc_strdup(NULL, "");
    for (int type = 0; type < STREAM_TYPE_COUNT; type++) {
        for (int n = 0; n < mpctx->num_tracks; n++) {
            struct track *track = mpctx->tracks[n];
            if (track->type != type)
                continue;
            h = talloc_asprintf_append_buffer(h, "%d-%d-%d-%d-%s\n", type,
                    track->user_tid, track->default_track, track->is_external,
                    track->lang ? track->lang : "");
        }
    }
    return h;
}

void mp_switch_track(struct MPContext *mpctx, enum stream_type type,
                     struct track *track)
{
    assert(!track || track->type == type);

    struct track *current = mpctx->current_track[type];
    if (track == current)
        return;

    if (type == STREAM_VIDEO) {
        int uninit = INITIALIZED_VCODEC;
        if (!mpctx->opts->force_vo)
            uninit |= mpctx->opts->fixed_vo && track ? 0 : INITIALIZED_VO;
        uninit_player(mpctx, uninit);
    } else if (type == STREAM_AUDIO) {
        uninit_player(mpctx, INITIALIZED_AO | INITIALIZED_ACODEC);
    } else if (type == STREAM_SUB) {
        uninit_player(mpctx, INITIALIZED_SUB);
    }

    mpctx->current_track[type] = track;

    int user_tid = track ? track->user_tid : -2;
    if (type == STREAM_VIDEO) {
        mpctx->opts->video_id = user_tid;
        reinit_video_chain(mpctx);
        mp_notify_property(mpctx, "vid");
    } else if (type == STREAM_AUDIO) {
        mpctx->opts->audio_id = user_tid;
        reinit_audio_chain(mpctx);
        mp_notify_property(mpctx, "aid");
    } else if (type == STREAM_SUB) {
        mpctx->opts->sub_id = user_tid;
        reinit_subs(mpctx);
        mp_notify_property(mpctx, "sid");
    }

    talloc_free(mpctx->track_layout_hash);
    mpctx->track_layout_hash = talloc_steal(mpctx, track_layout_hash(mpctx));
}

struct track *mp_track_by_tid(struct MPContext *mpctx, enum stream_type type,
                              int tid)
{
    if (tid == -1)
        return mpctx->current_track[type];
    for (int n = 0; n < mpctx->num_tracks; n++) {
        struct track *track = mpctx->tracks[n];
        if (track->type == type && track->user_tid == tid)
            return track;
    }
    return NULL;
}

bool mp_remove_track(struct MPContext *mpctx, struct track *track)
{
    if (track->under_timeline)
        return false;
    if (!track->is_external)
        return false;

    if (mpctx->current_track[track->type] == track) {
        mp_switch_track(mpctx, track->type, NULL);
        if (mpctx->current_track[track->type] == track)
            return false;
    }

    int index = 0;
    while (index < mpctx->num_tracks && mpctx->tracks[index] != track)
        index++;
    assert(index < mpctx->num_tracks);
    while (index + 1 < mpctx->num_tracks) {
        mpctx->tracks[index] = mpctx->tracks[index + 1];
        index++;
    }
    mpctx->num_tracks--;
    talloc_free(track);

    mp_notify(mpctx, MP_EVENT_TRACKS_CHANGED, NULL);

    return true;
}

static bool attachment_is_font(struct demux_attachment *att)
{
    if (!att->name || !att->type || !att->data || !att->data_size)
        return false;
    // match against MIME types
    if (strcmp(att->type, "application/x-truetype-font") == 0
        || strcmp(att->type, "application/x-font") == 0)
        return true;
    // fallback: match against file extension
    if (strlen(att->name) > 4) {
        char *ext = att->name + strlen(att->name) - 4;
        if (strcasecmp(ext, ".ttf") == 0 || strcasecmp(ext, ".ttc") == 0
            || strcasecmp(ext, ".otf") == 0)
            return true;
    }
    return false;
}

// Result numerically higher => better match. 0 == no match.
static int match_lang(char **langs, char *lang)
{
    for (int idx = 0; langs && langs[idx]; idx++) {
        if (lang && strcmp(langs[idx], lang) == 0)
            return INT_MAX - idx;
    }
    return 0;
}

/* Get the track wanted by the user.
 * tid is the track ID requested by the user (-2: deselect, -1: default)
 * lang is a string list, NULL is same as empty list
 * Sort tracks based on the following criteria, and pick the first:
 * 0) track matches tid (always wins)
 * 1) track is external
 * 1b) track was passed explicitly (is not an auto-loaded subtitle)
 * 2) earlier match in lang list
 * 3) track is marked default
 * 4) lower track number
 * If select_fallback is not set, 4) is only used to determine whether a
 * matching track is preferred over another track. Otherwise, always pick a
 * track (if nothing else matches, return the track with lowest ID).
 */
// Return whether t1 is preferred over t2
static bool compare_track(struct track *t1, struct track *t2, char **langs)
{
    if (t1->is_external != t2->is_external)
        return t1->is_external;
    if (t1->auto_loaded != t2->auto_loaded)
        return !t1->auto_loaded;
    int l1 = match_lang(langs, t1->lang), l2 = match_lang(langs, t2->lang);
    if (l1 != l2)
        return l1 > l2;
    if (t1->default_track != t2->default_track)
        return t1->default_track;
    if (t1->attached_picture != t2->attached_picture)
        return !t1->attached_picture;
    return t1->user_tid <= t2->user_tid;
}
static struct track *select_track(struct MPContext *mpctx,
                                  enum stream_type type, int tid, char **langs)
{
    if (tid == -2)
        return NULL;
    bool select_fallback = type == STREAM_VIDEO || type == STREAM_AUDIO;
    struct track *pick = NULL;
    for (int n = 0; n < mpctx->num_tracks; n++) {
        struct track *track = mpctx->tracks[n];
        if (track->type != type)
            continue;
        if (track->user_tid == tid)
            return track;
        if (!pick || compare_track(track, pick, langs))
            pick = track;
    }
    if (pick && !select_fallback && !pick->is_external
        && !match_lang(langs, pick->lang) && !pick->default_track)
        pick = NULL;
    if (pick && pick->attached_picture && !mpctx->opts->audio_display)
        pick = NULL;
    return pick;
}

// Normally, video/audio/sub track selection is persistent across files. This
// code resets track selection if the new file has a different track layout.
static void check_previous_track_selection(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;

    if (!mpctx->track_layout_hash)
        return;

    char *h = track_layout_hash(mpctx);
    if (strcmp(h, mpctx->track_layout_hash) != 0) {
        // Reset selection, but only if they're not "auto" or "off".
        if (opts->video_id >= 0)
            mpctx->opts->video_id = -1;
        if (opts->audio_id >= 0)
            mpctx->opts->audio_id = -1;
        if (opts->sub_id >= 0)
            mpctx->opts->sub_id = -1;
        talloc_free(mpctx->track_layout_hash);
        mpctx->track_layout_hash = NULL;
    }
    talloc_free(h);
}


static void open_subtitles_from_options(struct MPContext *mpctx)
{
    // after reading video params we should load subtitles because
    // we know fps so now we can adjust subtitle time to ~6 seconds AST
    // check .sub
    if (mpctx->opts->sub_name) {
        for (int i = 0; mpctx->opts->sub_name[i] != NULL; ++i)
            mp_add_subtitles(mpctx, mpctx->opts->sub_name[i]);
    }
    if (mpctx->opts->sub_auto) { // auto load sub file ...
        char **tmp = find_text_subtitles(mpctx->opts, mpctx->filename);
        int nsub = MP_TALLOC_ELEMS(tmp);
        for (int i = 0; i < nsub; i++) {
            char *filename = tmp[i];
            for (int n = 0; n < mpctx->num_sources; n++) {
                if (strcmp(mpctx->sources[n]->stream->url, filename) == 0)
                    goto skip;
            }
            struct track *track = mp_add_subtitles(mpctx, filename);
            if (track)
                track->auto_loaded = true;
        skip:;
        }
        talloc_free(tmp);
    }
}

static struct track *open_external_file(struct MPContext *mpctx, char *filename,
                                        char *demuxer_name, int stream_cache,
                                        enum stream_type filter)
{
    struct MPOpts *opts = mpctx->opts;
    if (!filename)
        return NULL;
    char *disp_filename = filename;
    if (strncmp(disp_filename, "memory://", 9) == 0)
        disp_filename = "memory://"; // avoid noise
    struct stream *stream = stream_open(filename, mpctx->opts);
    if (!stream)
        goto err_out;
    stream_enable_cache_percent(&stream, stream_cache,
                                opts->stream_cache_def_size,
                                opts->stream_cache_min_percent,
                                opts->stream_cache_seek_min_percent);
    struct demuxer_params params = {
        .ass_library = mpctx->ass_library, // demux_libass requires it
    };
    struct demuxer *demuxer =
        demux_open(stream, demuxer_name, &params, mpctx->opts);
    if (!demuxer) {
        free_stream(stream);
        goto err_out;
    }
    struct track *first = NULL;
    for (int n = 0; n < demuxer->num_streams; n++) {
        struct sh_stream *sh = demuxer->streams[n];
        if (sh->type == filter) {
            struct track *t = add_stream_track(mpctx, sh, false);
            t->is_external = true;
            t->title = talloc_strdup(t, disp_filename);
            t->external_filename = talloc_strdup(t, filename);
            first = t;
        }
    }
    if (!first) {
        free_demuxer(demuxer);
        free_stream(stream);
        MP_WARN(mpctx, "No streams added from file %s.\n",
                disp_filename);
        goto err_out;
    }
    MP_TARRAY_APPEND(NULL, mpctx->sources, mpctx->num_sources, demuxer);
    return first;

err_out:
    MP_ERR(mpctx, "Can not open external file %s.\n",
           disp_filename);
    return false;
}

static void open_audiofiles_from_options(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    open_external_file(mpctx, opts->audio_stream, opts->audio_demuxer_name,
                       opts->audio_stream_cache, STREAM_AUDIO);
}

struct track *mp_add_subtitles(struct MPContext *mpctx, char *filename)
{
    struct MPOpts *opts = mpctx->opts;
    return open_external_file(mpctx, filename, opts->sub_demuxer_name, 0,
                              STREAM_SUB);
}

static void open_subtitles_from_resolve(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct mp_resolve_result *res = mpctx->resolve_result;
    if (!res)
        return;
    for (int n = 0; n < res->num_subs; n++) {
        struct mp_resolve_sub *sub = res->subs[n];
        char *s = talloc_strdup(NULL, sub->url);
        if (!s)
            s = talloc_asprintf(NULL, "memory://%s", sub->data);
        struct track *t =
            open_external_file(mpctx, s, opts->sub_demuxer_name, 0, STREAM_SUB);
        talloc_free(s);
        if (t)
            t->lang = talloc_strdup(t, sub->lang);
    }
}


static void add_subtitle_fonts_from_sources(struct MPContext *mpctx)
{
#ifdef CONFIG_ASS
    if (mpctx->opts->ass_enabled) {
        for (int j = 0; j < mpctx->num_sources; j++) {
            struct demuxer *d = mpctx->sources[j];
            for (int i = 0; i < d->num_attachments; i++) {
                struct demux_attachment *att = d->attachments + i;
                if (mpctx->opts->use_embedded_fonts && attachment_is_font(att))
                    ass_add_font(mpctx->ass_library, att->name, att->data,
                                 att->data_size);
            }
        }
    }

    // libass seems to misbehave if fonts are changed while a renderer
    // exists, so we (re)create the renderer after fonts are set.
    assert(!mpctx->osd->ass_renderer);
    mpctx->osd->ass_renderer = ass_renderer_init(mpctx->osd->ass_library);
    if (mpctx->osd->ass_renderer)
        mp_ass_configure_fonts(mpctx->osd->ass_renderer,
                               mpctx->opts->sub_text_style);
#endif
}


// Replace the current playlist entry with playlist contents. Moves the entries
// from the given playlist pl, so the entries don't actually need to be copied.
static void transfer_playlist(struct MPContext *mpctx, struct playlist *pl)
{
    if (mpctx->demuxer->playlist->first) {
        playlist_transfer_entries(mpctx->playlist, mpctx->demuxer->playlist);
        if (mpctx->playlist->current)
            playlist_remove(mpctx->playlist, mpctx->playlist->current);
    } else {
        MP_WARN(mpctx, "Empty playlist!\n");
    }
}

static struct mp_resolve_result *resolve_url(const char *filename,
                                             struct MPOpts *opts)
{
#if defined(CONFIG_LIBQUVI) || defined(CONFIG_LIBQUVI9)
    return mp_resolve_quvi(filename, opts);
#else
    return NULL;
#endif
}

// Start playing the current playlist entry.
// Handle initialization and deinitialization.
static void play_current_file(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    double playback_start = -1e100;

    mpctx->initialized_flags |= INITIALIZED_PLAYBACK;

    mp_notify(mpctx, MP_EVENT_START_FILE, NULL);
    mp_flush_events(mpctx);

    mpctx->stop_play = 0;
    mpctx->filename = NULL;
    mpctx->shown_aframes = 0;
    mpctx->shown_vframes = 0;

    if (mpctx->playlist->current)
        mpctx->filename = mpctx->playlist->current->filename;

    if (!mpctx->filename)
        goto terminate_playback;

#ifdef CONFIG_ENCODING
    encode_lavc_discontinuity(mpctx->encode_lavc_ctx);
#endif

    mpctx->add_osd_seek_info &= OSD_SEEK_INFO_EDITION;

    if (opts->reset_options) {
        for (int n = 0; opts->reset_options[n]; n++) {
            const char *opt = opts->reset_options[n];
            if (opt[0]) {
                if (strcmp(opt, "all") == 0) {
                    m_config_backup_all_opts(mpctx->mconfig);
                } else {
                    m_config_backup_opt(mpctx->mconfig, opt);
                }
            }
        }
    }

    load_per_protocol_config(mpctx->mconfig, mpctx->filename);
    load_per_extension_config(mpctx->mconfig, mpctx->filename);
    load_per_file_config(mpctx->mconfig, mpctx->filename, opts->use_filedir_conf);

    if (opts->vo.video_driver_list)
        load_per_output_config(mpctx->mconfig, PROFILE_CFG_VO,
                               opts->vo.video_driver_list[0].name);
    if (opts->audio_driver_list)
        load_per_output_config(mpctx->mconfig, PROFILE_CFG_AO,
                               opts->audio_driver_list[0].name);

    if (opts->position_resume)
        load_playback_resume(mpctx->mconfig, mpctx->filename);

    load_per_file_options(mpctx->mconfig, mpctx->playlist->current->params,
                          mpctx->playlist->current->num_params);

    // We must enable getch2 here to be able to interrupt network connection
    // or cache filling
    if (opts->consolecontrols && !opts->slave_mode) {
        if (mpctx->initialized_flags & INITIALIZED_GETCH2)
            MP_WARN(mpctx, "WARNING: getch2_init called twice!\n");
        else
            getch2_enable();  // prepare stdin for hotkeys...
        mpctx->initialized_flags |= INITIALIZED_GETCH2;
        MP_DBG(mpctx, "\n[[[init getch2]]]\n");
    }

#ifdef CONFIG_ASS
    if (opts->ass_style_override)
        ass_set_style_overrides(mpctx->ass_library, opts->ass_force_style_list);
#endif

    MP_INFO(mpctx, "Playing: %s\n", mpctx->filename);

    //============ Open & Sync STREAM --- fork cache2 ====================

    assert(mpctx->stream == NULL);
    assert(mpctx->demuxer == NULL);
    assert(mpctx->sh_audio == NULL);
    assert(mpctx->sh_video == NULL);
    assert(mpctx->sh_sub == NULL);

    char *stream_filename = mpctx->filename;
    mpctx->resolve_result = resolve_url(stream_filename, opts);
    if (mpctx->resolve_result) {
        print_resolve_contents(mpctx->log, mpctx->resolve_result);
        if (mpctx->resolve_result->playlist) {
            transfer_playlist(mpctx, mpctx->resolve_result->playlist);
            goto terminate_playback;
        }
        stream_filename = mpctx->resolve_result->url;
    }
    mpctx->stream = stream_open(stream_filename, opts);
    if (!mpctx->stream) { // error...
        demux_was_interrupted(mpctx);
        goto terminate_playback;
    }
    mpctx->initialized_flags |= INITIALIZED_STREAM;

    mpctx->stream->start_pos += opts->seek_to_byte;

    if (opts->stream_dump && opts->stream_dump[0]) {
        stream_dump(mpctx);
        goto terminate_playback;
    }

    // CACHE2: initial prefill: 20%  later: 5%  (should be set by -cacheopts)
    int res = stream_enable_cache_percent(&mpctx->stream,
                                          opts->stream_cache_size,
                                          opts->stream_cache_def_size,
                                          opts->stream_cache_min_percent,
                                          opts->stream_cache_seek_min_percent);
    if (res == 0)
        if (demux_was_interrupted(mpctx))
            goto terminate_playback;

    stream_set_capture_file(mpctx->stream, opts->stream_capture);

#ifdef CONFIG_DVBIN
goto_reopen_demuxer: ;
#endif

    //============ Open DEMUXERS --- DETECT file type =======================

    mpctx->audio_delay = opts->audio_delay;

    mpctx->demuxer = demux_open(mpctx->stream, opts->demuxer_name, NULL, opts);
    mpctx->master_demuxer = mpctx->demuxer;
    if (!mpctx->demuxer) {
        MP_ERR(mpctx, "Failed to recognize file format.\n");
        goto terminate_playback;
    }

    MP_TARRAY_APPEND(NULL, mpctx->sources, mpctx->num_sources, mpctx->demuxer);

    mpctx->initialized_flags |= INITIALIZED_DEMUXER;

    if (mpctx->demuxer->playlist) {
        if (mpctx->demuxer->stream->safe_origin || opts->load_unsafe_playlists) {
            transfer_playlist(mpctx, mpctx->demuxer->playlist);
        } else {
            MP_ERR(mpctx, "\nThis looks like a playlist, but playlist support "
                   "will not be used automatically.\nThe main problem with "
                   "playlist safety is that playlist entries can be arbitrary,\n"
                   "and an attacker could make mpv poke around in your local "
                   "filesystem or network.\nUse --playlist=file or the "
                   "--load-unsafe-playlists option to load them anyway.\n");
        }
        goto terminate_playback;
    }

    if (mpctx->demuxer->matroska_data.ordered_chapters)
        build_ordered_chapter_timeline(mpctx);

    if (mpctx->demuxer->type == DEMUXER_TYPE_EDL)
        build_edl_timeline(mpctx);

    if (mpctx->demuxer->type == DEMUXER_TYPE_CUE)
        build_cue_timeline(mpctx);

    print_timeline(mpctx);

    if (mpctx->timeline) {
        // With Matroska, the "master" file usually dictates track layout etc.
        // On the contrary, the EDL and CUE demuxers are empty wrappers, as
        // well as Matroska ordered chapter playlist-like files.
        for (int n = 0; n < mpctx->num_timeline_parts; n++) {
            if (mpctx->timeline[n].source == mpctx->demuxer)
                goto main_is_ok;
        }
        mpctx->demuxer = mpctx->timeline[0].source;
    main_is_ok: ;
    }
    add_dvd_tracks(mpctx);
    add_demuxer_tracks(mpctx, mpctx->demuxer);

    mpctx->timeline_part = 0;
    if (mpctx->timeline)
        timeline_set_part(mpctx, mpctx->timeline_part, true);

    add_subtitle_fonts_from_sources(mpctx);

    open_subtitles_from_options(mpctx);
    open_subtitles_from_resolve(mpctx);
    open_audiofiles_from_options(mpctx);

    check_previous_track_selection(mpctx);

    mpctx->current_track[STREAM_VIDEO] =
        select_track(mpctx, STREAM_VIDEO, mpctx->opts->video_id, NULL);
    mpctx->current_track[STREAM_AUDIO] =
        select_track(mpctx, STREAM_AUDIO, mpctx->opts->audio_id,
                     mpctx->opts->audio_lang);
    mpctx->current_track[STREAM_SUB] =
        select_track(mpctx, STREAM_SUB, mpctx->opts->sub_id,
                     mpctx->opts->sub_lang);

    demux_info_print(mpctx->master_demuxer);
    print_file_properties(mpctx, mpctx->filename);

    preselect_demux_streams(mpctx);

#ifdef CONFIG_ENCODING
    if (mpctx->encode_lavc_ctx && mpctx->current_track[STREAM_VIDEO])
        encode_lavc_expect_stream(mpctx->encode_lavc_ctx, AVMEDIA_TYPE_VIDEO);
    if (mpctx->encode_lavc_ctx && mpctx->current_track[STREAM_AUDIO])
        encode_lavc_expect_stream(mpctx->encode_lavc_ctx, AVMEDIA_TYPE_AUDIO);
#endif

    reinit_video_chain(mpctx);
    reinit_audio_chain(mpctx);
    reinit_subs(mpctx);

    //================ SETUP STREAMS ==========================

    if (opts->force_fps && mpctx->sh_video) {
        mpctx->sh_video->fps = opts->force_fps;
        MP_INFO(mpctx, "FPS forced to be %5.3f.\n", mpctx->sh_video->fps);
    }

    //==================== START PLAYING =======================

    if (!mpctx->sh_video && !mpctx->sh_audio) {
        MP_FATAL(mpctx, "No video or audio streams selected.\n");
#ifdef CONFIG_DVBIN
        if (mpctx->stream->type == STREAMTYPE_DVB) {
            int dir;
            int v = mpctx->last_dvb_step;
            if (v > 0)
                dir = DVB_CHANNEL_HIGHER;
            else
                dir = DVB_CHANNEL_LOWER;

            if (dvb_step_channel(mpctx->stream, dir)) {
                mpctx->stop_play = PT_NEXT_ENTRY;
                mpctx->dvbin_reopen = 1;
            }
        }
#endif
        goto terminate_playback;
    }

    MP_VERBOSE(mpctx, "Starting playback...\n");

    mpctx->drop_frame_cnt = 0;
    mpctx->dropped_frames = 0;
    mpctx->max_frames = opts->play_frames;

    if (mpctx->max_frames == 0) {
        mpctx->stop_play = PT_NEXT_ENTRY;
        goto terminate_playback;
    }

    mpctx->time_frame = 0;
    mpctx->drop_message_shown = 0;
    mpctx->restart_playback = true;
    mpctx->video_pts = 0;
    mpctx->last_vo_pts = MP_NOPTS_VALUE;
    mpctx->last_seek_pts = 0;
    mpctx->playback_pts = MP_NOPTS_VALUE;
    mpctx->hrseek_active = false;
    mpctx->hrseek_framedrop = false;
    mpctx->step_frames = 0;
    mpctx->backstep_active = false;
    mpctx->total_avsync_change = 0;
    mpctx->last_chapter_seek = -2;
    mpctx->playing_msg_shown = false;
    mpctx->paused = false;
    mpctx->paused_for_cache = false;
    mpctx->seek = (struct seek_params){ 0 };

    // If there's a timeline force an absolute seek to initialize state
    double startpos = rel_time_to_abs(mpctx, opts->play_start, -1);
    if (startpos != -1 || mpctx->timeline) {
        queue_seek(mpctx, MPSEEK_ABSOLUTE, startpos, 0);
        execute_queued_seek(mpctx);
    }
    if (startpos == -1 && mpctx->resolve_result &&
        mpctx->resolve_result->start_time > 0)
    {
        queue_seek(mpctx, MPSEEK_ABSOLUTE, mpctx->resolve_result->start_time, 0);
        execute_queued_seek(mpctx);
    }
    if (opts->chapterrange[0] > 0) {
        if (mp_seek_chapter(mpctx, opts->chapterrange[0] - 1))
            execute_queued_seek(mpctx);
    }

    get_relative_time(mpctx); // reset current delta

    if (mpctx->opts->pause)
        pause_player(mpctx);

    playback_start = mp_time_sec();
    mpctx->error_playing = false;
    while (!mpctx->stop_play)
        run_playloop(mpctx);

    MP_VERBOSE(mpctx, "EOF code: %d  \n", mpctx->stop_play);

#ifdef CONFIG_DVBIN
    if (mpctx->dvbin_reopen) {
        mpctx->stop_play = 0;
        uninit_player(mpctx, INITIALIZED_ALL - (INITIALIZED_STREAM | INITIALIZED_GETCH2 | (opts->fixed_vo ? INITIALIZED_VO : 0)));
        mpctx->dvbin_reopen = 0;
        goto goto_reopen_demuxer;
    }
#endif

terminate_playback:  // don't jump here after ao/vo/getch initialization!

    if (mpctx->stop_play == KEEP_PLAYING)
        mpctx->stop_play = AT_END_OF_FILE;

    if (opts->position_save_on_quit && mpctx->stop_play == PT_QUIT)
        mp_write_watch_later_conf(mpctx);

    if (mpctx->step_frames)
        opts->pause = 1;

    MP_INFO(mpctx, "\n");

    // time to uninit all, except global stuff:
    int uninitialize_parts = INITIALIZED_ALL;
    if (opts->fixed_vo)
        uninitialize_parts -= INITIALIZED_VO;
    if ((opts->gapless_audio && mpctx->stop_play == AT_END_OF_FILE) ||
        mpctx->encode_lavc_ctx)
        uninitialize_parts -= INITIALIZED_AO;
    uninit_player(mpctx, uninitialize_parts);

    // xxx handle this as INITIALIZED_CONFIG?
    if (mpctx->stop_play != PT_RESTART)
        m_config_restore_backups(mpctx->mconfig);

    mpctx->filename = NULL;
    talloc_free(mpctx->resolve_result);
    mpctx->resolve_result = NULL;

#ifdef CONFIG_ASS
    if (mpctx->osd->ass_renderer)
        ass_renderer_done(mpctx->osd->ass_renderer);
    mpctx->osd->ass_renderer = NULL;
    ass_clear_fonts(mpctx->ass_library);
#endif

    // Played/paused for longer than 3 seconds -> ok
    bool playback_short = mpctx->stop_play == AT_END_OF_FILE &&
                (playback_start < 0 || mp_time_sec() - playback_start < 3.0);
    bool init_failed = mpctx->stop_play == AT_END_OF_FILE &&
                (mpctx->shown_aframes == 0 && mpctx->shown_vframes == 0);
    if (mpctx->playlist->current && !mpctx->playlist->current_was_replaced) {
        mpctx->playlist->current->playback_short = playback_short;
        mpctx->playlist->current->init_failed = init_failed;
    }

    mp_notify(mpctx, MP_EVENT_TRACKS_CHANGED, NULL);
    mp_notify(mpctx, MP_EVENT_END_FILE, NULL);
    mp_flush_events(mpctx);
}

// Determine the next file to play. Note that if this function returns non-NULL,
// it can have side-effects and mutate mpctx.
//  direction: -1 (previous) or +1 (next)
//  force: if true, don't skip playlist entries marked as failed
struct playlist_entry *mp_next_file(struct MPContext *mpctx, int direction,
                                    bool force)
{
    struct playlist_entry *next = playlist_get_next(mpctx->playlist, direction);
    if (next && direction < 0 && !force) {
        // Don't jump to files that would immediately go to next file anyway
        while (next && next->playback_short)
            next = next->prev;
        // Always allow jumping to first file
        if (!next && mpctx->opts->loop_times < 0)
            next = mpctx->playlist->first;
    }
    if (!next && mpctx->opts->loop_times >= 0) {
        if (direction > 0) {
            if (mpctx->opts->shuffle)
                playlist_shuffle(mpctx->playlist);
            next = mpctx->playlist->first;
            if (next && mpctx->opts->loop_times > 1) {
                mpctx->opts->loop_times--;
                if (mpctx->opts->loop_times == 1)
                    mpctx->opts->loop_times = -1;
            }
        } else {
            next = mpctx->playlist->last;
            // Don't jump to files that would immediately go to next file anyway
            while (next && next->playback_short)
                next = next->prev;
        }
        if (!force && next && next->init_failed) {
            // Don't endless loop if no file in playlist is playable
            bool all_failed = true;
            struct playlist_entry *cur;
            for (cur = mpctx->playlist->first; cur; cur = cur->next) {
                all_failed &= cur->init_failed;
                if (!all_failed)
                    break;
            }
            if (all_failed)
                next = NULL;
        }
    }
    return next;
}

// Play all entries on the playlist, starting from the current entry.
// Return if all done.
static void play_files(struct MPContext *mpctx)
{
    mpctx->quit_player_rc = EXIT_NONE;
    for (;;) {
        idle_loop(mpctx);
        if (mpctx->stop_play == PT_QUIT)
            break;

        mpctx->error_playing = true;
        play_current_file(mpctx);
        if (mpctx->error_playing) {
            if (!mpctx->quit_player_rc) {
                mpctx->quit_player_rc = EXIT_NOTPLAYED;
            } else if (mpctx->quit_player_rc == EXIT_PLAYED) {
                mpctx->quit_player_rc = EXIT_SOMENOTPLAYED;
            }
        } else if (mpctx->quit_player_rc == EXIT_NOTPLAYED) {
            mpctx->quit_player_rc = EXIT_SOMENOTPLAYED;
        } else {
            mpctx->quit_player_rc = EXIT_PLAYED;
        }
        if (mpctx->stop_play == PT_QUIT)
            break;

        if (!mpctx->stop_play || mpctx->stop_play == AT_END_OF_FILE)
            mpctx->stop_play = PT_NEXT_ENTRY;

        struct playlist_entry *new_entry = NULL;

        if (mpctx->stop_play == PT_NEXT_ENTRY) {
            new_entry = mp_next_file(mpctx, +1, false);
        } else if (mpctx->stop_play == PT_CURRENT_ENTRY) {
            new_entry = mpctx->playlist->current;
        } else if (mpctx->stop_play == PT_RESTART) {
            // The same as PT_CURRENT_ENTRY, unless we decide that the current
            // playlist entry can be removed during playback.
            new_entry = mpctx->playlist->current;
        } else { // PT_STOP
            playlist_clear(mpctx->playlist);
        }

        mpctx->playlist->current = new_entry;
        mpctx->playlist->current_was_replaced = false;
        mpctx->stop_play = 0;

        if (!mpctx->playlist->current && !mpctx->opts->player_idle_mode)
            break;
    }
}

// Abort current playback and set the given entry to play next.
// e must be on the mpctx->playlist.
void mp_set_playlist_entry(struct MPContext *mpctx, struct playlist_entry *e)
{
    assert(playlist_entry_to_index(mpctx->playlist, e) >= 0);
    mpctx->playlist->current = e;
    mpctx->playlist->current_was_replaced = false;
    mpctx->stop_play = PT_CURRENT_ENTRY;
}
