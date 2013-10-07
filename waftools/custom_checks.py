from checks import *

__all__ = ["check_libsmbclient"]

def check_libsmbclient():
    def fn(ctx, dependency_identifier):
        libdl  = " ".join(ctx.env.LIB_LIBDL)
        stmtfn = check_statement('libsmbclient.h', 'smbc_opendir("smb://")')
        flibs  = ['smbclient {0}', 'smbclient nsl {0}', 'smbclient ssl nsl {0}']
        libs   = [lib.format(libdl) for lib in flibs]
        return check_libs(flibs, stmtfn)(ctx, dependency_identifier)
    return fn
