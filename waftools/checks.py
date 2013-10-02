any_version = None

def even(n):
    return n % 2 == 0

def check_cc(**kw):
    def fn(ctx, dependency_identifier):
        argsl    = list(args)
        packages = [i for i in args if argsl.index(i) % 2 == 0]
        sargs    = [i for i in args if i] # remove None
        return ctx.check_cfg(package=" ".join(packages),
                             args=sargs + ["--libs", "--cflags"],
                             uselib_store=dependency_identifier,
                             mandatory=False)
    return fn


def check_pkg_config(*args):
    def fn(ctx, dependency_identifier):
        argsl    = list(args)
        packages = [el for (i, el) in enumerate(args) if even(i)]
        sargs    = [i for i in args if i] # remove None
        return ctx.check_cfg(package=" ".join(packages),
                             args=sargs + ["--libs", "--cflags"],
                             uselib_store=dependency_identifier,
                             mandatory=False)
    return fn
