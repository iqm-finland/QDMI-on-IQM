<!-- Entries in each category are sorted by merge time, with the latest PRs appearing first. -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on a mixture of [Keep a Changelog] and [Common Changelog].
This project adheres to [Semantic Versioning], with the exception that minor
releases may include breaking changes.

## [Unreleased]

### Added

- 🐍 Start building CPython 3.15 wheels ([#177]) ([**@denialhaag**])
- ✨ Add a `licenses` keyword argument to `iqm.qdmi.offloader`'s `sample`/
  `estimate`, forwarded as `--licenses` on `srun`, so callers can request the
  Slurm license a site administrator may require via the SPANK plugin's
  `iqm_require_license` option ([#162]) ([**@marcelwa**])
- ✨ Support retrieving existing IQM circuit jobs by ID ([#160])
  ([**@burgholzer**])

### Fixed

- 🐛 Serialize move-gate and active-reset options using the canonical IQM
  RunRequest field names ([#169]) ([**@burgholzer**])
- 🩹 Preserve exact program bytes across QDMI job parameter updates and property
  queries ([#159]) ([**@burgholzer**])
- 🩹 Keep IQM shot and histogram bitstrings in the measurement-key and qubit
  order specified by IQM's result metadata ([#158]) ([**@burgholzer**])

### Changed

- ⚡️ Reuse HTTP connections within each QDMI device session to reduce TCP/TLS
  setup during initialization and subsequent requests ([#163])
  ([**@burgholzer**])

## [1.3.0] - 2026-07-31

### Added

- ✨ Validate IQM backend and target-QC availability once per node for each
  Slurm job step before launching tasks ([#136]) ([**@burgholzer**])
- ✨ Export the stable IQM device ID and symbol prefix on the installed CMake
  target so MQT Core can synthesize relocatable manifests ([#140])
  ([**@burgholzer**])
- ✨ Add `iqm.qdmi.offloader` module exposing programmatic `sample` and
  `estimate` functions (including `qc_id`/`qc_alias` SPANK device-selection
  parameters) for Slurm job submissions ([#104], [#130], [#133])
  ([**@marcelwa**])
- ✨ Validate Slurm `--licenses` alignment with the targeted QC alias in the
  SPANK plugin, enabling admins to enforce Slurm-native concurrency limits on
  on-premise QCs ([#114], [#134]) ([**@marcelwa**], [**@burgholzer**])
- 📝 Add an integration scenarios analysis comparing IQM integration paths
  (direct C++/Python usage, Slurm + SPANK offloading, Spack install, and
  non-Slurm schedulers as forward-looking guidance) and an administrator guide
  walking through standing up IQM access on a Slurm cluster ([#147])
  ([**@marcelwa**])

### Changed

- ⚡️ Cache SPANK validation per node and job step, isolate it from daemon-only
  environment variables, bound each launch-time request to 30 seconds by
  default, emit task diagnostics once per node and step, and require Slurm 20.02
  or newer ([#136]) ([**@burgholzer**])
- ⬆️ Update `mqt-core` to version 3.8.0 and use its stable device registry with
  per-backend device sessions ([#140]) ([**@burgholzer**])
- ♻️ Further align C++ HTTP and authentication handling with [cpr] abstractions
  ([#122]) ([**@burgholzer**])

### Fixed

- 🩹 Preserve a configured `iqm.default` endpoint unless `IQMBackend` receives
  an explicit or environment-provided base URL ([#140]) ([**@burgholzer**])
- 🩹 Fix rate limit handling and retry logic for API requests ([#122])
  ([**@burgholzer**])

## [1.2.0] - 2026-07-09

### Added

- ✨ Expose the current calibration set ID as a device property via
  `QDMI_DEVICE_PROPERTY_CUSTOM1` ([#108]) ([**@burgholzer**])
- ✨ Add `iqm-sampler` and `iqm-estimator` CLI entrypoints leveraging
  `IQMBackend`'s primitives ([#92]) ([**@marcelwa**])
- ✨ Implement Slurm SPANK plugin for injecting IQM environment variables and
  session parameters into Slurm jobs ([#74], [#117]) ([**@marcelwa**])
- ✨ Support environment variable fallbacks (`IQM_BASE_URL`, `IQM_QC_ID`, and
  `IQM_QC_ALIAS`) for session initialization ([#74]) ([**@marcelwa**])

### Fixed

- 🩹 Ensure the QDMI device can handle devices with computational resonators
  ([#107]) ([**@burgholzer**])

### Changed

- ⬆️ Update `mqt-core` to version 3.7.0 ([#120]) ([**@denialhaag**])
- ⬆️ Update QDMI to version 1.3.2 ([#120]) ([**@denialhaag**])
- ♻️ Rewrite the C++ HTTP client to use [cpr] instead of direct `libcurl` calls
  ([#105]) ([**@marcelwa**])
- ♻️ Considerably simplify the internal HTTP client implementation ([#105])
  ([**@burgholzer**])

## [1.1.1] - 2026-06-01

### Fixed

- 🩹 Decouple MQT Core and Qiskit dependency resolution to avoid potential
  cyclic dependency issues ([#78]) ([**@marcelwa**])

## [1.1.0] - 2026-05-22

### Added

- ✨ Add end-to-end examples and documentation for running experiments on IQM
  hardware ([#53]) ([**@marcelwa**], [**@burgholzer**])
- 🚸 Add explicit retry logic to avoid hitting API rate limits ([#52], [#73])
  ([**@marcelwa**], [**@burgholzer**])
- ✨ Add Qiskit-compatible `IQMBackend` wrapper including Sampler and Estimator
  primitives ([#37]) ([**@marcelwa**], [**@burgholzer**])

### Changed

- ♻️ Consistently use `IQM_TOKEN` instead of `RESONANCE_API_KEY` for
  authentication ([#67]) ([**@marcelwa**])
- ♻️ Consistently enforce explicit authentication parameters to take precedence
  over environment variables ([#67]) ([**@marcelwa**])
- 📝 Restructure documentation for better clarity and navigation ([#53])
  ([**@burgholzer**])
- 📝 Update Spack package guide with latest release and installation
  instructions ([#53]) ([**@burgholzer**])
- 📝 Reduce redundancy across documentation ([#53]) ([**@burgholzer**])
- 🚚 Replace `meetiqm.com` with new `iqm.tech` domain in documentation and
  codebase ([#56]) ([**@iqmtjm**], [**@marcelwa**])
- 🚸 Demote warnings from missing calibration endpoints to debug level ([#51])
  ([**@marcelwa**])

## [1.0.1] - 2026-04-27

### Fixed

- 🐛 Fix CD workflow runner ([#49]) ([**@burgholzer**])

## [1.0.0] - 2026-04-27

Compatible with QDMI `v1.3.0`.

- 🎉 Initial release ([**@burgholzer**], [**@marcelwa**])

<!-- Version links -->

[Unreleased]: https://github.com/iqm-finland/QDMI-on-IQM/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/iqm-finland/QDMI-on-IQM/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/iqm-finland/QDMI-on-IQM/compare/v1.1.1...v1.2.0
[1.1.1]: https://github.com/iqm-finland/QDMI-on-IQM/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/iqm-finland/QDMI-on-IQM/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/iqm-finland/QDMI-on-IQM/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/iqm-finland/QDMI-on-IQM/compare/...v1.0.0

<!-- PR links -->

[#177]: https://github.com/iqm-finland/QDMI-on-IQM/pull/177
[#169]: https://github.com/iqm-finland/QDMI-on-IQM/pull/169
[#163]: https://github.com/iqm-finland/QDMI-on-IQM/pull/163
[#162]: https://github.com/iqm-finland/QDMI-on-IQM/pull/162
[#159]: https://github.com/iqm-finland/QDMI-on-IQM/pull/159
[#158]: https://github.com/iqm-finland/QDMI-on-IQM/pull/158
[#160]: https://github.com/iqm-finland/QDMI-on-IQM/pull/160
[#147]: https://github.com/iqm-finland/QDMI-on-IQM/pull/147
[#140]: https://github.com/iqm-finland/QDMI-on-IQM/pull/140
[#136]: https://github.com/iqm-finland/QDMI-on-IQM/pull/136
[#134]: https://github.com/iqm-finland/QDMI-on-IQM/pull/134
[#133]: https://github.com/iqm-finland/QDMI-on-IQM/pull/133
[#130]: https://github.com/iqm-finland/QDMI-on-IQM/pull/130
[#122]: https://github.com/iqm-finland/QDMI-on-IQM/pull/122
[#120]: https://github.com/iqm-finland/QDMI-on-IQM/pull/120
[#117]: https://github.com/iqm-finland/QDMI-on-IQM/pull/117
[#114]: https://github.com/iqm-finland/QDMI-on-IQM/pull/114
[#108]: https://github.com/iqm-finland/QDMI-on-IQM/pull/108
[#107]: https://github.com/iqm-finland/QDMI-on-IQM/pull/107
[#105]: https://github.com/iqm-finland/QDMI-on-IQM/pull/105
[#104]: https://github.com/iqm-finland/QDMI-on-IQM/pull/104
[#92]: https://github.com/iqm-finland/QDMI-on-IQM/pull/92
[#78]: https://github.com/iqm-finland/QDMI-on-IQM/pull/78
[#74]: https://github.com/iqm-finland/QDMI-on-IQM/pull/74
[#73]: https://github.com/iqm-finland/QDMI-on-IQM/pull/73
[#67]: https://github.com/iqm-finland/QDMI-on-IQM/pull/67
[#56]: https://github.com/iqm-finland/QDMI-on-IQM/pull/56
[#53]: https://github.com/iqm-finland/QDMI-on-IQM/pull/53
[#52]: https://github.com/iqm-finland/QDMI-on-IQM/pull/52
[#51]: https://github.com/iqm-finland/QDMI-on-IQM/pull/51
[#49]: https://github.com/iqm-finland/QDMI-on-IQM/pull/49
[#37]: https://github.com/iqm-finland/QDMI-on-IQM/pull/37

<!-- Contributor -->

[**@burgholzer**]: https://github.com/burgholzer
[**@marcelwa**]: https://github.com/marcelwa
[**@iqmtjm**]: https://github.com/iqmtjm
[**@denialhaag**]: https://github.com/denialhaag

<!-- General links -->

[cpr]: https://github.com/libcpr/cpr
[Keep a Changelog]: https://keepachangelog.com/en/1.1.0/
[Common Changelog]: https://common-changelog.org
[Semantic Versioning]: https://semver.org/spec/v2.0.0.html
