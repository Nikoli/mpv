def pkgconfig_repr(name, version):
    if version:
        return " ".join([name, version])
    else:
        return name

# XXX: this can be done with decorators if i remember python correctly
def check_pkg_config(package, version=None):
    def fn(ctx):
        args = [pkgconfig_repr(package, version)]
        return ctx.check_cfg(package=package,
                             args=args + ["--libs", "--cflags"],
                             mandatory=False)
    return fn
