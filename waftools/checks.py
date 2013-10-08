import os

__all__ = [
    "check_pkg_config", "check_cc", "check_statement", "check_libs",
    "check_headers", "check_true", "any_version", "load_fragment"]

any_version = None

def even(n):
    return n % 2 == 0

def define_options(dependency_identifier):
    return {'define_name':  ("have_" + dependency_identifier).upper()}

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
        opts = merge_options(dependency_identifier,
                             {'fragment':fragment},
                             define_options(dependency_identifier), kw)
        return ctx.check_cc(**opts)
    return fn

def check_cc(**kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        options = merge_options(dependency_identifier, kw_ext, kw)
        return ctx.check_cc(**options)
    return fn

def check_pkg_config(*args, **kw):
    def fn(ctx, dependency_identifier):
        argsl    = list(args)
        packages = [el for (i, el) in enumerate(args) if even(i)]
        sargs    = [i for i in args if i] # remove None
        defaults = {
            'package': " ".join(packages),
            'args': sargs + ["--libs", "--cflags"] }
        opts = merge_options(dependency_identifier, defaults, kw)
        return ctx.check_cfg(**opts)
    return fn

def check_headers(*headers):
    def fn(ctx, dependency_identifier):
        for header in headers:
            defaults = {'header_name': header, 'features': 'c cprogram'}
            options  = merge_options(dependency_identifier, defaults)
            if ctx.check(**options):
                return True
        return False
    return fn

def check_true(ctx, dependency_identifier):
    return True

def load_fragment(fragment):
    file_path = os.path.join(os.path.dirname(__file__), 'fragments',
                             fragment + '.c')
    fp = open(file_path,"r")
    fragment_code = fp.read()
    fp.close()
    return fragment_code
