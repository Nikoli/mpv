from checks import *

def x86_32(ctx):
    ctx.define('ARCH_X86', 1)
    ctx.define('ARCH_X86_32', 1)

def x86_64(ctx):
    ctx.define('ARCH_X86', 1)
    ctx.define('ARCH_X86_64', 1)
    ctx.define('HAVE_FAST_64BIT', 1)

def ia64(ctx):
    ctx.define('HAVE_FAST_64BIT', 1)

def default(ctx):
    pass

def configure(ctx):
    ctx.define('ARCH_X86', 0)
    ctx.define('ARCH_X86_32', 0)
    ctx.define('ARCH_X86_64', 0)
    ctx.define('HAVE_FAST_64BIT', 0)
    {
        'x86_32': x86_32,
        'x86_64': x86_64,
        'ia':     ia64
    }.get(ctx.env.DEST_CPU, default)(ctx)
