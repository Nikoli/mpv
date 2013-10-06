from waflib.Configure import conf

@conf
def find_gcc(conf):
    """
    Only difference from the waf official function, is it tries `cc` before
    `gcc` and `clang` is added after `gcc`.
    """
    cc = conf.find_program(['cc', 'gcc', 'clang'], var='CC')
    cc = conf.cmd_to_list(cc)
    conf.get_cc_version(cc, gcc=True)
    conf.env.CC_NAME = 'gcc'
    conf.env.CC      = cc

@conf
def get_config_header(self, defines=True, headers=False, define_prefix=''):
    """
    Only difference is it outputs `#define VAR 0` or `#define VAR value`
    instead of `#undef VAR` or `#define VAR val`.
    """
    from waflib.Tools.c_config import DEFKEYS, INCKEYS
    lst = []
    if headers:
        for x in self.env[INCKEYS]:
            lst.append('#include <%s>' % x)

    if defines:
        for x in self.env[DEFKEYS]:
            val = self.is_defined(x) and self.get_define(x) or "0"
            lst.append('#define %s %s' % (define_prefix + x, val))

    return "\n".join(lst)
