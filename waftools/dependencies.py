from waflib.Errors import ConfigurationError, WafError
from waflib.Configure import conf
from waflib.Build import BuildContext
from waflib.Logs import pprint
from inflectors import DependencyInflector

class DependencyError(Exception):
    pass

class Dependency(object):
    def __init__(self, ctx, satisfied_deps, dependency):
        self.ctx = ctx
        self.satisfied_deps = satisfied_deps
        self.identifier, self.attributes = dependency['name'], dependency

    def check(self):
        self.ctx.start_msg('Checking for {0}'.format(self.attributes['desc']))

        try:
            self.check_disabled()
            self.check_dependencies()
            self.check_negative_dependencies()
        except DependencyError:
            # No check was run, since the prerequisites of the dependency are
            # not satisfied. Make sure the define is 'undefined' so that we
            # get a `#define YYY 0` in `config.h`.
            def_key = DependencyInflector(self.identifier).define_key()
            self.ctx.undefine(def_key)
            return

        self.check_autodetect_func()

    def check_disabled(self):
        if not self.enabled_option():
            self.skip()
            raise DependencyError

    def check_dependencies(self):
        if 'deps' in self.attributes:
            deps = set(self.attributes['deps'])
            if not deps <= self.satisfied_deps:
                missing_deps = deps - self.satisfied_deps
                self.skip("{0} not found".format(", ".join(missing_deps)))
                raise DependencyError

    def check_negative_dependencies(self):
        if 'deps_neg' in self.attributes:
            deps = set(self.attributes['deps_neg'])
            if deps <= self.satisfied_deps:
                conflicting_deps = deps & self.satisfied_deps
                self.skip("{0} found".format(", ".join(conflicting_deps)), 'CYAN')
                raise DependencyError

    def check_autodetect_func(self):
        if self.attributes['func'](self.ctx, self.identifier):
            self.success(self.identifier)
        else:
            self.fail()
            self.fatal_if_needed()

    def enabled_option(self):
        try:
            return getattr(self.ctx.options, self.enabled_option_repr())
        except AttributeError:
            pass
        return True

    def enabled_option_repr(self):
        return "enable_{0}".format(self.identifier)

    def success(self, depname):
        self.satisfied_deps.add(depname)
        self.ctx.end_msg('yes')

    def fail(self, reason='no'):
        self.ctx.end_msg(reason, 'RED')

    def fatal_if_needed(self):
        if self.attributes.get('req', False):
            raise ConfigurationError(self.attributes['fmsg'])

    def skip(self, reason='disabled', color='YELLOW'):
        self.ctx.end_msg(reason, color)

def check_dependency(ctx, dependency):
    Dependency(ctx, ctx.env.satisfied_deps, dependency).check()


def __detect_target_os_dependency__(ctx):
    target = "os_{0}".format(ctx.env.DEST_OS)
    ctx.start_msg('Detected target OS:')
    ctx.end_msg(target)
    ctx.env.satisfied_deps.add(target)

def configure(ctx):
    ctx.env['satisfied_deps'] = set()
    __detect_target_os_dependency__(ctx)

@conf
def parse_dependencies(ctx, dependencies):
    [check_dependency(ctx, dependency) for dependency in dependencies]

def filtered_sources(ctx, sources):
    def source_file(source):
        if isinstance(source, tuple):
            return source[0]
        else:
            return source

    def unpack_and_check_dependency(source):
        try:
            _, dependency = source
            if set([dependency]) <= ctx.env.satisfied_deps:
                return True
            else:
                return False
        except ValueError:
            return True

    return [source_file(source) for source in sources \
            if unpack_and_check_dependency(source)]

def env_fetch(tx):
    def fn(ctx):
        lists = [ctx.env[tx(dep)] for dep in ctx.env.satisfied_deps \
                    if (tx(dep) in ctx.env)]
        return filter(None, sum(lists, []))
    return fn

BuildContext.filtered_sources = filtered_sources
BuildContext.dependencies_includes  = env_fetch(lambda x: "INCLUDES_{0}".format(x))
BuildContext.dependencies_lib       = env_fetch(lambda x: "LIB_{0}".format(x))
BuildContext.dependencies_libpath   = env_fetch(lambda x: "LIBPATH_{0}".format(x))
BuildContext.dependencies_framework = env_fetch(lambda x: "FRAMEWORK_{0}".format(x))
