def __get_version__(ctx):
    import subprocess
    process = subprocess.Popen("./version.sh --print",
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               cwd=ctx.srcnode.abspath(), shell=True)
    process.wait()
    (version, err) = process.communicate()
    return version.strip()

def __get_build_date__():
    import time
    return time.strftime("%a %b %d %H:%M:%S %Z %Y", time.gmtime())

def __write_config_h__(ctx):
    ctx.start_msg("Writing configuration header:")
    ctx.write_config_header('config.h')
    ctx.end_msg("config.h", "PINK")

def __write_version_h__(ctx):
    ctx.start_msg("Writing header:")
    ctx.define("VERSION",   __get_version__(ctx))
    ctx.define("BUILDDATE", __get_build_date__())
    ctx.write_config_header("version.h")
    ctx.end_msg("version.h", "PINK")

def configure(ctx):
    __write_config_h__(ctx)
    __write_version_h__(ctx)
