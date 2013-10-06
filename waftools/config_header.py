from waflib.Configure import conf
from waflib.Tools.c_config import DEFKEYS, INCKEYS

@conf
def get_config_header(self, defines=True, headers=False, define_prefix=''):
    lst = []
    if headers:
        for x in self.env[INCKEYS]:
            lst.append('#include <%s>' % x)

    if defines:
        for x in self.env[DEFKEYS]:
            if self.is_defined(x):
                val = self.get_define(x)
                lst.append('#define %s %s' % (define_prefix + x, val))
            else:
                lst.append('#define %s %d' % (define_prefix + x, 0))

    return "\n".join(lst)
