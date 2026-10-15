// Entry point for every native test suite. Include it first, once per test folder.
//
// PlatformIO reads doctest's console output to count assertions, so successes are reported
// and the exit code is left to PlatformIO.
#pragma once

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

int main(int argc, char** argv) {
    doctest::Context context;
    context.setOption("success", true);
    context.setOption("no-exitcode", true);
    context.applyCommandLine(argc, argv);
    return context.run();
}
