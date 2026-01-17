![Karatala](assets/images/karatala.png)
[![](https://github.com/karatala/karatala/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/karatala/karatala/actions)

Karatala is a drum kit engine instrument for e-Drummers and everyone.


## Development
based on Pamplejuce

init:
`git submodule update --init --recursive`

1. Runs C++23
2. Uses JUCE 8.x as a git submodule (tracking develop).
3. Uses CPM for dependency management.
3. Relies on CMake 3.25 and higher for cross-platform building.
4. Has [Catch2](https://github.com/catchorg/Catch2) v3.7.1 for the test framework and runner.
5. Includes a `Tests` target and a `Benchmarks` target with examples to get started quickly.
6. Has [Melatonin Inspector](https://github.com/sudara/melatonin_inspector) installed as a JUCE module to help relieve headaches when building plugin UI.

It also has integration with GitHub Actions, specifically:

1. Building and testing cross-platform (linux, macOS, Windows) binaries
2. Running tests and benchmarks in CI
3. Running [pluginval](http://github.com/tracktion/pluginval) 1.x against the binaries for plugin validation
4. Config for [installing Intel IPP](https://www.intel.com/content/www/us/en/developer/tools/oneapi/ipp.html)
5. [Code signing and notarization on macOS](https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/)
6. [Windows code signing via Azure Trusted Signing](https://melatonin.dev/blog/code-signing-on-windows-with-azure-trusted-signing/)

It also contains:

1. A `.gitignore` for all platforms.
2. A `.clang-format` file for keeping code tidy.
3. A `VERSION` file that will propagate through JUCE and your app.
4. A ton of useful comments and options around the CMake config.

