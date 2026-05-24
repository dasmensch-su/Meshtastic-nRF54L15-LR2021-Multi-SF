"""PlatformIO build script (pre: runs before other Meshtastic scripts).

ClusterFuzzLite executes in a different container from the build. During the build,
attempt to link statically to as many dependencies as possible. For dependencies that
do not have static libraries, the shared library files are copied to the output
directory by the build.sh script.
"""

import glob
import os
import shlex

from SCons.Script import DefaultEnvironment, Literal

env = DefaultEnvironment()

cxxflags = shlex.split(os.getenv("CXXFLAGS"))
sanitizer_flags = shlex.split(os.getenv("SANITIZER_FLAGS"))
lib_fuzzing_engine = shlex.split(os.getenv("LIB_FUZZING_ENGINE"))
statics = glob.glob("/usr/lib/lib*.a") + glob.glob("/usr/lib/*/lib*.a")
no_static = set(("-ldl",))


def replaceStatic(lib):
    """Replace -l<libname> with the static .a file for the library."""
    if not lib.startswith("-l") or lib in no_static:
        return lib
    static_name = f"/lib{lib[2:]}.a"
    static = [s for s in statics if s.endswith(static_name)]
    if len(static) == 1:
        return static[0]
    return lib


# Setup the environment for building with Clang and the OSS-Fuzz required build flags.
# MESHTASTIC_MULTI_SF_BRIDGE=1 is enabled unconditionally so fuzzers that
# target Multi-SF-specific code paths (shadow_channels_fuzzer.cpp and any
# future siblings) link against the real symbols. The router_fuzzer is
# unaffected because its input path never enters the Multi-SF branches
# without explicit shaping; the only cost is a slightly larger binary.
env.Append(
    CFLAGS=os.getenv("CFLAGS"),
    CXXFLAGS=cxxflags,
    CPPDEFINES=[("MESHTASTIC_MULTI_SF_BRIDGE", "1")],
    LIBSOURCE_DIRS=["/usr/lib/x86_64-linux-gnu"],
    LINKFLAGS=cxxflags
    + sanitizer_flags
    + lib_fuzzing_engine
    + ["-stdlib=libc++", "-std=c++17"],
    _LIBFLAGS=[replaceStatic(s) for s in shlex.split(os.getenv("STATIC_LIBS"))]
    + [
        "/usr/lib/x86_64-linux-gnu/libunistring.a",  # Needs to be at the end.
        # Find the shared libraries in a subdirectory named lib
        # within the same directory as the binary.
        Literal("-Wl,-rpath,$ORIGIN/lib"),
        "-Wl,-z,origin",
    ],
)
