def pkgconfig_repr(args):
    return " ".join(args)

# XXX: this can be done with decorators if i remember python correctly
def check_pkg_config_multiple(*args):
    def fn(ctx):
        sargs = [pkgconfig_repr(args[1:])]
        return ctx.check_cfg(package=args[0],
                             args=sargs + ["--libs", "--cflags"],
                             mandatory=False)
    return fn

def check_pkg_config_multiple(*args):
    def fn(ctx):
        sargs = [pkgconfig_repr(args[1:])]
        return ctx.check_cfg(package=args[0],
                             args=sargs + ["--libs", "--cflags"],
                             mandatory=False)
    return fn


def check_pkg_config(package, version=None):
    def fn(ctx):
        args = [pkgconfig_repr([package, version])]
        return ctx.check_cfg(package=package,
                             args=args + ["--libs", "--cflags"],
                             mandatory=False)
    return fn
