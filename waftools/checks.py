any_version = None

def even(n):
    return n % 2 == 0

def merge_options(dependency_identifier, *args):
    initial_values = {
        'uselib_store': dependency_identifier,
        'mandatory': False }

    def merge_dicts(r, n):
        return n and dict(r.items() + n.items()) or r

    return reduce(merge_dicts, args, initial_values)

def check_libs(libs, function):
    libs = [""] + libs
    def fn(ctx, dependency_identifier):
        for lib in libs:
            if function(ctx, dependency_identifier, lib=lib):
                return True
        return False
    return fn

def check_statement(header, statement):
    def fn(ctx, dependency_identifier, **kw):
        fragment = """
            #include <{0}>
            int main(int argc, char **argv)
            {{ {1}; return 0; }} """.format(header, statement)
        opts = merge_options(dependency_identifier, {'fragment':fragment}, kw)
        return ctx.check_cc(**opts)
    return fn

def check_cc(**kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        options = merge_options(dependency_identifier, kw_ext, kw)
        return ctx.check_cc(**options)
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
