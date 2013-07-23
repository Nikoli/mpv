import os, sys, imp, types
from waflib import Utils, Configure
from pkgconfig import pkgconfig_check, pkgconfig_check_list, pkgconfig_check_one

libav_packages = {
    "libavfilter":  (">= 3.17.0", False),
    "libavdevice":  (">= 54.0.0", False),
    "libpostproc":  (">= 52.0.0", False),
    "libavutil":    ("> 51.73.0", True),
    "libavcodec":   ("> 54.34.0", True),
    "libavformat":  ("> 54.19.0", True),
    "libswscale":   (">= 2.0.0",  True),
}

resamplers = {
    "libavresample": (">= 1.0.0",    False),
    "libswresample": (">= 0.15.100", False),
}

def configure(ctx):
    pkgconfig_check_list(ctx, libav_packages)
    pkgconfig_check_one(ctx, resamplers)
