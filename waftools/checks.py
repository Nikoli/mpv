import os
from inflectors import DependencyInflector

__all__ = [
    "check_pkg_config", "check_cc", "check_statement", "check_libs",
    "check_headers", "compose_checks", "check_true", "any_version",
    "load_fragment"]

any_version = None

def even(n):
    return n % 2 == 0

def __define_options__(dependency_identifier):
    return DependencyInflector(dependency_identifier).define_dict()

def __merge_options__(dependency_identifier, *args):
    initial_values = DependencyInflector(dependency_identifier).storage_dict()
    initial_values['mandatory'] = False

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

def check_statement(header, statement, **kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        fragment = """
            #include <{0}>
            int main(int argc, char **argv)
            {{ {1}; return 0; }} """.format(header, statement)
        opts = __merge_options__(dependency_identifier,
                                 {'fragment':fragment},
                                 __define_options__(dependency_identifier),
                                 kw_ext, kw)
        return ctx.check_cc(**opts)
    return fn

def check_cc(**kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        options = __merge_options__(dependency_identifier,
                                    __define_options__(dependency_identifier),
                                    kw_ext, kw)
        return ctx.check_cc(**options)
    return fn

def check_pkg_config(*args, **kw_ext):
    def fn(ctx, dependency_identifier, **kw):
        argsl    = list(args)
        packages = [el for (i, el) in enumerate(args) if even(i)]
        sargs    = [i for i in args if i] # remove None
        defaults = {
            'package': " ".join(packages),
            'args': sargs + ["--libs", "--cflags"] }
        opts = __merge_options__(dependency_identifier, defaults, kw_ext, kw)
        return ctx.check_cfg(**opts)
    return fn

def check_headers(*headers):
    def fn(ctx, dependency_identifier):
        for header in headers:
            defaults = {'header_name': header, 'features': 'c cprogram'}
            options  = __merge_options__(dependency_identifier, defaults)
            if ctx.check(**options):
                return True
        return False
    return fn

def check_true(ctx, dependency_identifier):
    defkey = DependencyInflector(dependency_identifier).define_key()
    ctx.define(defkey, 1)
    return True

def compose_checks(*checks):
    def fn(ctx, dependency_identifier):
        return all([check(ctx, dependency_identifier) for check in checks])
    return fn

def load_fragment(fragment):
    file_path = os.path.join(os.path.dirname(__file__), 'fragments',
                             fragment + '.c')
    fp = open(file_path,"r")
    fragment_code = fp.read()
    fp.close()
    return fragment_code
