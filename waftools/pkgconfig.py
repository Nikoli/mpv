from msg import fmsg

def pkgconfig_repr(name, version):
    if version:
        return " ".join([name, version])
    else:
        return name

def pkgconfig_check(ctx, package, version, mandatory):
    args = [pkgconfig_repr(package, version)]
    return ctx.check_cfg(package=package,
                         args=args + ["--libs", "--cflags"],
                         msg=fmsg(args),
                         mandatory=mandatory)

def pkgconfig_repr_list(pkglist):
    return [pkgconfig_repr(package, version)
                for package, (version, mandatory) in pkglist.items()]

def pkgconfig_check_package_iter(ctx, package, pkglist):
    version, mandatory = pkglist[package]
    return pkgconfig_check(ctx, package, version, mandatory)

def pkgconfig_check_list(ctx, pkglist):
    return [pkgconfig_check_package_iter(ctx, package, pkglist)
                for package in sorted(pkglist)]

def pkgconfig_check_one(ctx, pkglist):
    if not any(pkgconfig_check_list(ctx, pkglist)):
        req = ", ".join(pkgconfig_repr_list(pkglist))
        raise ctx.errors.ConfigurationError("One of ( %s ) is required" % req)
