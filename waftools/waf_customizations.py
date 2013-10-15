from waflib.Configure import conf

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
