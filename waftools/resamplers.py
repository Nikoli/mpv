import os, sys, imp, types
from waflib import Utils, Configure
from msg import fmsg

resamplers = {
    "libswresample": ">= 0.15.100",
    "libavresample": ">= 1.0.0",
}

def resampler_repr(resampler, version):
    return " ".join([resampler, version])

def try_resampler(ctx, resampler, version):
    args = [resampler_repr(resampler, version)]
    return ctx.check_cfg(package=resampler,
                         args=args + ["--libs", "--cflags"],
                         msg=fmsg(args), mandatory=False)

def configure(ctx):
    if  try_resampler(ctx, "libswresample", resamplers["libswresample"]):
        ctx.env.MPV_LIB     += ctx.env.LIB_LIBSWRESAMPLE
        ctx.env.MPV_LIBPATH += ctx.env.LIBPATH_LIBSWRESAMPLE
    elif try_resampler(ctx, "libavresample", resamplers["libavresample"]):
        ctx.env.MPV_LIB     += ctx.env.LIB_LIBAVRESAMPLE
        ctx.env.MPV_LIBPATH += ctx.env.LIBPATH_LIBAVRESAMPLE
    else:
        rs = " or ".join([resampler_repr(k,v) for k,v in resamplers.items()])
        raise ctx.errors.ConfigurationError("Either %s required" % rs)

