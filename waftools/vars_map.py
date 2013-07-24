def configure(ctx):
    # Map internal mpv defines to waf internal, eventually these will be 
    # synched by replacing mpv source with waf-internal.
    config_map = {
        "HAVE_LIBAVRESAMPLE": "CONFIG_LIBAVRESAMPLE",
        "HAVE_LIBSWRESAMPLE": "CONFIG_LIBSWRESAMPLE",
        "HAVE_LIBASS":        "CONFIG_ASS",
    }

    for key in config_map:
        if key in ctx.env.define_key:
            ctx.define(config_map[key], key, quote=False)
