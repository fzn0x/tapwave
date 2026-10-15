"""PlatformIO post script: link the native-asan test binaries with the sanitizer runtimes."""

Import("env")  # noqa: F821  (provided by PlatformIO)

env.Append(LINKFLAGS=["-fsanitize=address,undefined"])  # noqa: F821
