
static int read_keys(void *ctx, int fd)
{
    if (getch2(ctx))
        return MP_INPUT_NOTHING;
    return MP_INPUT_DEAD;
}

static void init_input(struct MPContext *mpctx)
{
    mpctx->input = mp_input_init(mpctx->global);
    if (mpctx->opts->slave_mode)
        mp_input_add_cmd_fd(mpctx->input, 0, USE_FD0_CMD_SELECT, MP_INPUT_SLAVE_CMD_FUNC, NULL);
    else if (mpctx->opts->consolecontrols)
        mp_input_add_key_fd(mpctx->input, 0, 1, read_keys, NULL, mpctx->input);
    // Set the libstream interrupt callback
    stream_set_interrupt_callback(mp_input_check_interrupt, mpctx->input);

#ifdef CONFIG_COCOA
    cocoa_set_input_context(mpctx->input);
#endif
}

void mp_print_version(int always)
{
    int v = always ? MSGL_INFO : MSGL_V;
    mp_msg(MSGT_CPLAYER, v,
           "%s (C) 2000-2013 mpv/MPlayer/mplayer2 projects\n built on %s\n", mplayer_version, mplayer_builddate);
    print_libav_versions(v);
    mp_msg(MSGT_CPLAYER, v, "\n");
}

static bool handle_help_options(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    int opt_exit = 0;
    if (opts->audio_decoders && strcmp(opts->audio_decoders, "help") == 0) {
        struct mp_decoder_list *list = mp_audio_decoder_list();
        mp_print_decoders(MSGT_CPLAYER, MSGL_INFO, "Audio decoders:", list);
        talloc_free(list);
        opt_exit = 1;
    }
    if (opts->video_decoders && strcmp(opts->video_decoders, "help") == 0) {
        struct mp_decoder_list *list = mp_video_decoder_list();
        mp_print_decoders(MSGT_CPLAYER, MSGL_INFO, "Video decoders:", list);
        talloc_free(list);
        opt_exit = 1;
    }
#ifdef CONFIG_X11
    if (opts->vo.fstype_list && strcmp(opts->vo.fstype_list[0], "help") == 0) {
        fstype_help();
        mp_msg(MSGT_FIXME, MSGL_FIXME, "\n");
        opt_exit = 1;
    }
#endif
    if ((opts->demuxer_name && strcmp(opts->demuxer_name, "help") == 0) ||
        (opts->audio_demuxer_name && strcmp(opts->audio_demuxer_name, "help") == 0) ||
        (opts->sub_demuxer_name && strcmp(opts->sub_demuxer_name, "help") == 0)) {
        demuxer_help();
        MP_INFO(mpctx, "\n");
        opt_exit = 1;
    }
    if (opts->list_properties) {
        property_print_help();
        opt_exit = 1;
    }
#ifdef CONFIG_ENCODING
    if (encode_lavc_showhelp(mpctx->opts))
        opt_exit = 1;
#endif
    return opt_exit;
}

#ifdef PTW32_STATIC_LIB
static void detach_ptw32(void)
{
    pthread_win32_thread_detach_np();
    pthread_win32_process_detach_np();
}
#endif

static void osdep_preinit(int *p_argc, char ***p_argv)
{
    char *enable_talloc = getenv("MPV_LEAK_REPORT");
    if (*p_argc > 1 && (strcmp((*p_argv)[1], "-leak-report") == 0 ||
                        strcmp((*p_argv)[1], "--leak-report") == 0))
        enable_talloc = "1";
    if (enable_talloc && strcmp(enable_talloc, "1") == 0)
        talloc_enable_leak_report();

#ifdef __MINGW32__
    mp_get_converted_argv(p_argc, p_argv);
#endif

#ifdef PTW32_STATIC_LIB
    pthread_win32_process_attach_np();
    pthread_win32_thread_attach_np();
    atexit(detach_ptw32);
#endif

#if defined(__MINGW32__) || defined(__CYGWIN__)
    // stop Windows from showing all kinds of annoying error dialogs
    SetErrorMode(0x8003);
#endif

    load_termcap(NULL); // load key-codes

    mp_time_init();
}

/* This preprocessor directive is a hack to generate a mplayer-nomain.o object
 * file for some tools to link against. */
#ifndef DISABLE_MAIN
static int mpv_main(int argc, char *argv[])
{
    osdep_preinit(&argc, &argv);

    if (argc >= 1) {
        argc--;
        argv++;
    }

    struct MPContext *mpctx = talloc(NULL, MPContext);
    *mpctx = (struct MPContext){
        .last_dvb_step = 1,
        .terminal_osd_text = talloc_strdup(mpctx, ""),
        .playlist = talloc_struct(mpctx, struct playlist, {0}),
    };

    // Create the config context and register the options
    mpctx->mconfig = m_config_new(mpctx, sizeof(struct MPOpts),
                                  &mp_default_opts, mp_opts, NULL);
    mpctx->opts = mpctx->mconfig->optstruct;
    mpctx->mconfig->includefunc = cfg_include;
    mpctx->mconfig->use_profiles = true;

    struct MPOpts *opts = mpctx->opts;


    mpctx->global = talloc_zero(mpctx, struct mpv_global);
    mpctx->global->opts = opts;

    // Nothing must call mp_msg() before this
    mp_msg_init(mpctx->global);
    mpctx->log = mp_log_new(mpctx, mpctx->global->log, "!cplayer");

    init_libav();
    GetCpuCaps(&gCpuCaps);
    screenshot_init(mpctx);
    mpctx->mixer = mixer_init(mpctx, opts);
    command_init(mpctx);

    // Preparse the command line
    m_config_preparse_command_line(mpctx->mconfig, argc, argv);

    mp_print_version(false);

    if (!parse_cfgfiles(mpctx, mpctx->mconfig))
        exit_player(mpctx, EXIT_ERROR);

    int r = m_config_parse_mp_command_line(mpctx->mconfig, mpctx->playlist,
                                           argc, argv);
    if (r < 0) {
        if (r <= M_OPT_EXIT) {
            exit_player(mpctx, EXIT_NONE);
        } else {
            exit_player(mpctx, EXIT_ERROR);
        }
    }

    if (handle_help_options(mpctx))
        exit_player(mpctx, EXIT_NONE);

    MP_VERBOSE(mpctx, "Configuration: " CONFIGURATION "\n");
    MP_VERBOSE(mpctx, "Command line:");
    for (int i = 0; i < argc; i++)
        MP_VERBOSE(mpctx, " '%s'", argv[i]);
    MP_VERBOSE(mpctx, "\n");

    if (!mpctx->playlist->first && !opts->player_idle_mode) {
        mp_print_version(true);
        MP_INFO(mpctx, "%s", mp_gtext(mp_help_text));
        exit_player(mpctx, EXIT_NONE);
    }

#ifdef CONFIG_PRIORITY
    set_priority();
#endif

    init_input(mpctx);

#ifdef CONFIG_ENCODING
    if (opts->encode_output.file && *opts->encode_output.file) {
        mpctx->encode_lavc_ctx = encode_lavc_init(&opts->encode_output);
        if(!mpctx->encode_lavc_ctx) {
            mp_msg(MSGT_VO, MSGL_INFO, "Encoding initialization failed.");
            exit_player(mpctx, EXIT_ERROR);
        }
        m_config_set_option0(mpctx->mconfig, "vo", "lavc");
        m_config_set_option0(mpctx->mconfig, "ao", "lavc");
        m_config_set_option0(mpctx->mconfig, "fixed-vo", "yes");
        m_config_set_option0(mpctx->mconfig, "force-window", "no");
        m_config_set_option0(mpctx->mconfig, "gapless-audio", "yes");
        mp_input_enable_section(mpctx->input, "encode", MP_INPUT_EXCLUSIVE);
    }
#endif

#ifdef CONFIG_ASS
    mpctx->ass_library = mp_ass_init(opts);
#else
    MP_WARN(mpctx, "Compiled without libass.\n");
    MP_WARN(mpctx, "There will be no OSD and no text subs.\n");
#endif

    mpctx->osd = osd_create(opts, mpctx->ass_library);

    if (opts->force_vo) {
        opts->fixed_vo = 1;
        mpctx->video_out = init_best_video_out(mpctx->global, mpctx->input,
                                               mpctx->encode_lavc_ctx);
        if (!mpctx->video_out) {
            MP_FATAL(mpctx, "Error opening/initializing "
                    "the selected video_out (-vo) device.\n");
            exit_player(mpctx, EXIT_ERROR);
        }
        mpctx->mouse_cursor_visible = true;
        mpctx->initialized_flags |= INITIALIZED_VO;
    }

#ifdef CONFIG_LUA
    // Lua user scripts can call arbitrary functions. Load them at a point
    // where this is safe.
    mp_lua_init(mpctx);
#endif

    if (opts->shuffle)
        playlist_shuffle(mpctx->playlist);

    mpctx->playlist->current = mp_resume_playlist(mpctx->playlist, opts);
    if (!mpctx->playlist->current)
        mpctx->playlist->current = mpctx->playlist->first;

    play_files(mpctx);

    exit_player(mpctx, mpctx->stop_play == PT_QUIT ? EXIT_QUIT : mpctx->quit_player_rc);

    return 1;
}

int main(int argc, char *argv[])
{
#ifdef CONFIG_COCOA
    return cocoa_main(mpv_main, argc, argv);
#else
    return mpv_main(argc, argv);
#endif
}

#endif /* DISABLE_MAIN */


static MP_NORETURN void exit_player(struct MPContext *mpctx,
                                    enum exit_reason how)
{
    int rc;
    uninit_player(mpctx, INITIALIZED_ALL);

#ifdef CONFIG_ENCODING
    encode_lavc_finish(mpctx->encode_lavc_ctx);
    encode_lavc_free(mpctx->encode_lavc_ctx);
#endif

    mpctx->encode_lavc_ctx = NULL;

#ifdef CONFIG_LUA
    mp_lua_uninit(mpctx);
#endif

#if defined(__MINGW32__) || defined(__CYGWIN__)
    timeEndPeriod(1);
#endif

#ifdef CONFIG_COCOA
    cocoa_set_input_context(NULL);
#endif

    command_uninit(mpctx);

    mp_input_uninit(mpctx->input);

    osd_free(mpctx->osd);

#ifdef CONFIG_ASS
    ass_library_done(mpctx->ass_library);
    mpctx->ass_library = NULL;
#endif

    if (how != EXIT_NONE) {
        const char *reason;
        switch (how) {
        case EXIT_SOMENOTPLAYED:
        case EXIT_PLAYED:
            reason = "End of file";
            break;
        case EXIT_NOTPLAYED:
            reason = "No files played";
            break;
        case EXIT_ERROR:
            reason = "Fatal error";
            break;
        default:
            reason = "Quit";
        }
        MP_INFO(mpctx, "\nExiting... (%s)\n", reason);
    }

    if (mpctx->has_quit_custom_rc) {
        rc = mpctx->quit_custom_rc;
    } else {
        switch (how) {
            case EXIT_ERROR:
                rc = 1; break;
            case EXIT_NOTPLAYED:
                rc = 2; break;
            case EXIT_SOMENOTPLAYED:
                rc = 3; break;
            default:
                rc = 0;
        }
    }

    // must be last since e.g. mp_msg uses option values
    // that will be freed by this.

    mp_msg_uninit(mpctx->global);
    talloc_free(mpctx);

#ifdef CONFIG_COCOA
    terminate_cocoa_application();
    // never reach here:
    // terminate calls exit itself, just silence compiler warning
    exit(0);
#else
    exit(rc);
#endif
}
