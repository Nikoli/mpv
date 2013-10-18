
typedef struct mp_osd_msg mp_osd_msg_t;
struct mp_osd_msg {
    /// Previous message on the stack.
    mp_osd_msg_t *prev;
    /// Message text.
    char *msg;
    int id, level, started;
    /// Display duration in seconds.
    double time;
    // Show full OSD for duration of message instead of msg
    // (osd_show_progression command)
    bool show_position;
};

// time is in ms
static mp_osd_msg_t *add_osd_msg(struct MPContext *mpctx, int id, int level,
                                 int time)
{
    rm_osd_msg(mpctx, id);
    mp_osd_msg_t *msg = talloc_struct(mpctx, mp_osd_msg_t, {
        .prev = mpctx->osd_msg_stack,
        .msg = "",
        .id = id,
        .level = level,
        .time = time / 1000.0,
    });
    mpctx->osd_msg_stack = msg;
    return msg;
}

static void set_osd_msg_va(struct MPContext *mpctx, int id, int level, int time,
                           const char *fmt, va_list ap)
{
    if (level == OSD_LEVEL_INVISIBLE)
        return;
    mp_osd_msg_t *msg = add_osd_msg(mpctx, id, level, time);
    msg->msg = talloc_vasprintf(msg, fmt, ap);
}

void set_osd_msg(struct MPContext *mpctx, int id, int level, int time,
                 const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_osd_msg_va(mpctx, id, level, time, fmt, ap);
    va_end(ap);
}

void set_osd_tmsg(struct MPContext *mpctx, int id, int level, int time,
                  const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_osd_msg_va(mpctx, id, level, time, mp_gtext(fmt), ap);
    va_end(ap);
}

/**
 *  \brief Remove a message from the OSD stack
 *
 *  This function can be used to get rid of a message right away.
 *
 */

void rm_osd_msg(struct MPContext *mpctx, int id)
{
    mp_osd_msg_t *msg, *last = NULL;

    // Search for the msg
    for (msg = mpctx->osd_msg_stack; msg && msg->id != id;
         last = msg, msg = msg->prev) ;
    if (!msg)
        return;

    // Detach it from the stack and free it
    if (last)
        last->prev = msg->prev;
    else
        mpctx->osd_msg_stack = msg->prev;
    talloc_free(msg);
}

/**
 *  \brief Get the current message from the OSD stack.
 *
 *  This function decrements the message timer and destroys the old ones.
 *  The message that should be displayed is returned (if any).
 *
 */

static mp_osd_msg_t *get_osd_msg(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    mp_osd_msg_t *msg, *prev, *last = NULL;
    double now = mp_time_sec();
    double diff;
    char hidden_dec_done = 0;

    if (mpctx->osd_visible && now >= mpctx->osd_visible) {
        mpctx->osd_visible = 0;
        mpctx->osd->progbar_type = -1; // disable
        osd_changed(mpctx->osd, OSDTYPE_PROGBAR);
    }
    if (mpctx->osd_function_visible && now >= mpctx->osd_function_visible) {
        mpctx->osd_function_visible = 0;
        mpctx->osd_function = 0;
    }

    if (!mpctx->osd_last_update)
        mpctx->osd_last_update = now;
    diff = now >= mpctx->osd_last_update ? now - mpctx->osd_last_update : 0;

    mpctx->osd_last_update = now;

    // Look for the first message in the stack with high enough level.
    for (msg = mpctx->osd_msg_stack; msg; last = msg, msg = prev) {
        prev = msg->prev;
        if (msg->level > opts->osd_level && hidden_dec_done)
            continue;
        // The message has a high enough level or it is the first hidden one
        // in both cases we decrement the timer or kill it.
        if (!msg->started || msg->time > diff) {
            if (msg->started)
                msg->time -= diff;
            else
                msg->started = 1;
            // display it
            if (msg->level <= opts->osd_level)
                return msg;
            hidden_dec_done = 1;
            continue;
        }
        // kill the message
        talloc_free(msg);
        if (last) {
            last->prev = prev;
            msg = last;
        } else {
            mpctx->osd_msg_stack = prev;
            msg = NULL;
        }
    }
    // Nothing found
    return NULL;
}

// type: mp_osd_font_codepoints, ASCII, or OSD_BAR_*
// name: fallback for terminal OSD
void set_osd_bar(struct MPContext *mpctx, int type, const char *name,
                 double min, double max, double val)
{
    struct MPOpts *opts = mpctx->opts;
    if (opts->osd_level < 1 || !opts->osd_bar_visible)
        return;

    if (mpctx->video_out && opts->term_osd != 1) {
        mpctx->osd_visible = mp_time_sec() + opts->osd_duration / 1000.0;
        mpctx->osd->progbar_type = type;
        mpctx->osd->progbar_value = (val - min) / (max - min);
        mpctx->osd->progbar_num_stops = 0;
        osd_changed(mpctx->osd, OSDTYPE_PROGBAR);
        return;
    }

    set_osd_msg(mpctx, OSD_MSG_BAR, 1, opts->osd_duration, "%s: %d %%",
                name, ROUND(100 * (val - min) / (max - min)));
}

// Update a currently displayed bar of the same type, without resetting the
// timer.
static void update_osd_bar(struct MPContext *mpctx, int type,
                           double min, double max, double val)
{
    if (mpctx->osd->progbar_type == type) {
        float new_value = (val - min) / (max - min);
        if (new_value != mpctx->osd->progbar_value) {
            mpctx->osd->progbar_value = new_value;
            osd_changed(mpctx->osd, OSDTYPE_PROGBAR);
        }
    }
}

static void set_osd_bar_chapters(struct MPContext *mpctx, int type)
{
    struct osd_state *osd = mpctx->osd;
    osd->progbar_num_stops = 0;
    if (osd->progbar_type == type) {
        double len = get_time_length(mpctx);
        if (len > 0) {
            int num = get_chapter_count(mpctx);
            for (int n = 0; n < num; n++) {
                double time = chapter_start_time(mpctx, n);
                if (time >= 0) {
                    float pos = time / len;
                    MP_TARRAY_APPEND(osd, osd->progbar_stops,
                                     osd->progbar_num_stops, pos);
                }
            }
        }
    }
}

void set_osd_function(struct MPContext *mpctx, int osd_function)
{
    struct MPOpts *opts = mpctx->opts;

    mpctx->osd_function = osd_function;
    mpctx->osd_function_visible = mp_time_sec() + opts->osd_duration / 1000.0;
}

/**
 * \brief Display text subtitles on the OSD
 */
static void set_osd_subtitle(struct MPContext *mpctx, const char *text)
{
    if (!text)
        text = "";
    if (strcmp(mpctx->osd->sub_text, text) != 0) {
        osd_set_sub(mpctx->osd, text);
        if (!mpctx->video_out) {
            rm_osd_msg(mpctx, OSD_MSG_SUB_BASE);
            if (text && text[0])
                set_osd_msg(mpctx, OSD_MSG_SUB_BASE, 1, INT_MAX, "%s", text);
        }
    }
    if (!text[0])
        rm_osd_msg(mpctx, OSD_MSG_SUB_BASE);
}


/**
 * \brief Update the OSD message line.
 *
 * This function displays the current message on the vo OSD or on the term.
 * If the stack is empty and the OSD level is high enough the timer
 * is displayed (only on the vo OSD).
 *
 */

static void update_osd_msg(struct MPContext *mpctx)
{
    struct MPOpts *opts = mpctx->opts;
    struct osd_state *osd = mpctx->osd;

    add_seek_osd_messages(mpctx);
    update_osd_bar(mpctx, OSD_BAR_SEEK, 0, 1,
                   av_clipf(get_current_pos_ratio(mpctx, false), 0, 1));

    // Look if we have a msg
    mp_osd_msg_t *msg = get_osd_msg(mpctx);
    if (msg && !msg->show_position) {
        if (mpctx->video_out && opts->term_osd != 1) {
            osd_set_text(osd, msg->msg);
        } else if (opts->term_osd) {
            if (strcmp(mpctx->terminal_osd_text, msg->msg)) {
                talloc_free(mpctx->terminal_osd_text);
                mpctx->terminal_osd_text = talloc_strdup(mpctx, msg->msg);
                // Multi-line message => clear what will be the second line
                write_status_line(mpctx, "");
                mp_msg(MSGT_CPLAYER, MSGL_STATUS, "%s%s\n", opts->term_osd_esc,
                       mpctx->terminal_osd_text);
                print_status(mpctx);
            }
        }
        return;
    }

    int osd_level = opts->osd_level;
    if (msg && msg->show_position)
        osd_level = 3;

    if (mpctx->video_out && opts->term_osd != 1) {
        // fallback on the timer
        char *text = NULL;

        if (osd_level >= 2)
            sadd_osd_status(&text, mpctx, osd_level == 3);

        osd_set_text(osd, text);
        talloc_free(text);
        return;
    }

    // Clear the term osd line
    if (opts->term_osd && mpctx->terminal_osd_text[0]) {
        mpctx->terminal_osd_text[0] = '\0';
        mp_msg(MSGT_CPLAYER, MSGL_STATUS, "%s\n", opts->term_osd_esc);
    }
}

