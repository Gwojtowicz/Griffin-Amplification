# Contributing to Griffin Amplification

Thanks for your interest! Bug reports, ideas and pull requests are all welcome.

## Ideas and bugs

- **Have an idea?** [Open a feature request](https://github.com/Gwojtowicz/Griffin-Amplification/issues/new?template=feature_request.md)
  and describe what you'd like to see, so we can talk it through before you start on it.
- **Found a bug?** [Open a bug report](https://github.com/Gwojtowicz/Griffin-Amplification/issues/new?template=bug_report.md)
  with your version number (bottom-right corner of the main window), your audio interface, sample rate and buffer size.
- **Want to work on an existing issue?** Say so in its thread first, so two people don't do the same work.

## Building

See [Building from source](README.md#building-from-source) in the README and [the manual](docs/MANUAL.md#building).

## Before you open a pull request

There aren't automated tests yet. Please check by hand that:

- [ ] The standalone app and the VST3 build.
- [ ] The app makes sound, and loads a `.nam` capture and a cab IR.
- [ ] Presets saved by the previous version still load and sound the same.
  (New parameters go at the end of the `EParams` enum and are saved by name; see
  [Saved-state format](docs/MANUAL.md#saved-state-format).)
- [ ] The VST3 passes Steinberg's [VST3 Plug-in Test Host](https://steinbergmedia.github.io/vst3_dev_portal/pages/What+is+the+VST+3+SDK/Plug-in+Test+Host.html) tests.
- [ ] You've bumped `PLUG_VERSION_STR` / `PLUG_VERSION_HEX` in `NeuralAmpModeler/config.h` and added a
  [CHANGELOG.md](CHANGELOG.md) entry.

## Code style

C++ (`.cpp` and `.h`) is formatted with `clang-format` (LLVM style, see `.clang-format`). Run

```bash
bash format.bash
```

before committing, so a pull request only shows real changes.

## License

By contributing, you agree that your contributions are licensed under the project's [MIT license](LICENSE).
