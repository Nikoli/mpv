from checks import *

__all__ = ["check_pthreads", "check_pthreads_w32_static", "check_iconv",
"check_libsmbclient"]

pthreads_program = load_fragment('pthreads')

def check_pthreads(ctx, dependency_identifier):
    platform_cflags = {
        'linux':   '-D_REENTRANT',
        'freebsd': '-D_THREAD_SAFE',
        'netbsd':  '-D_THREAD_SAFE',
        'openbsd': '-D_THREAD_SAFE',
    }.get(ctx.env.DEST_OS, '')
    # XXX: the configure script also checks for just 'pthread' is that
    # really needed?
    libs    = ['lpthreadGC2', 'pthread']
    checkfn = check_cc(fragment=pthreads_program, cflags=platform_cflags)
    return check_libs(libs, checkfn)(ctx, dependency_identifier)

def check_pthreads_w32_static(ctx, dependency_identifier):
    platform_cflags = '-DPTW32_STATIC_LIB'
    libs    = ['lpthreadGC2 lws2_32']
    checkfn = check_cc(fragment=pthreads_program, cflags=platform_cflags)
    return check_libs(libs, checkfn)(ctx, dependency_identifier)

def check_iconv(ctx, dependency_identifier):
    iconv_program = load_fragment('iconv')
    libdliconv = " ".join(ctx.env.LIB_LIBDL + ['iconv'])
    libs       = ['iconv', libdliconv]
    checkfn = check_cc(fragment=iconv_program)
    return check_libs(libs, checkfn)(ctx, dependency_identifier)

def check_libsmbclient(ctx, dependency_identifier):
    libdl  = " ".join(ctx.env.LIB_LIBDL)
    stmtfn = check_statement('libsmbclient.h', 'smbc_opendir("smb://")')
    flibs  = ['smbclient {0}', 'smbclient nsl {0}', 'smbclient ssl nsl {0}']
    libs   = [lib.format(libdl) for lib in flibs]
    return check_libs(flibs, stmtfn)(ctx, dependency_identifier)
