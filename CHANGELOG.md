<!-- Entries in each category are sorted by merge time, with the latest PRs appearing first. -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on a mixture of [Keep a Changelog] and [Common Changelog].
This project adheres to [Semantic Versioning], with the exception that minor releases may include breaking changes.

## [Unreleased]

## Added

- ✨ Add end-to-end examples and documentation for running experiments on IQM hardware ([#53]) ([**@marcelwa**], [**@burgholzer**])
- 🚸 Add explicit retry logic to avoid hitting API rate limits ([#52], [#73]) ([**@marcelwa**], [**@burgholzer**])
- ✨ Add Qiskit-compatible `IQMBackend` wrapper including Sampler and Estimator primitives ([#37]) ([**@marcelwa**], [**@burgholzer**])

## Changed

- 📝 Restructure documentation for better clarity and navigation ([#53]) ([**@burgholzer**])
- 📝 Update Spack package guide with latest release and installation instructions ([#53]) ([**@burgholzer**])
- 📝 Reduce redundancy across documentation ([#53]) ([**@burgholzer**])
- 🚚 Replace `meetiqm.com` with new `iqm.tech` domain in documentation and codebase ([#56]) ([**@iqmtjm**], [**@marcelwa**])
- 🚸 Demote warnings from missing calibration endpoints to debug level ([#51]) ([**@marcelwa**])

## Changed

- ♻️ Consistently use `IQM_TOKEN` instead of `RESONANCE_API_KEY` for authentication ([#67]) ([**@marcelwa**])
- ♻️ Consistently enforce explicit authentication parameters to take precedence over environment variables ([#67]) ([**@marcelwa**])

## [1.0.1] - 2026-04-27

### Fixed

- 🐛 Fix CD workflow runner ([#49]) ([**@burgholzer**])

## [1.0.0] - 2026-04-27

Compatible with QDMI `v1.3.0`.

- 🎉 Initial release ([**@burgholzer**], [**@marcelwa**])

<!-- Version links -->

[Unreleased]: https://github.com/iqm-finland/QDMI-on_IQM/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/iqm-finland/QDMI-on_IQM/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/iqm-finland/QDMI-on_IQM/compare/...v1.0.0

<!-- PR links -->

[#73]: https://github.com/iqm-finland/QDMI-on_IQM/pull/73
[#67]: https://github.com/iqm-finland/QDMI-on-IQM/pull/67
[#56]: https://github.com/iqm-finland/QDMI-on_IQM/pull/56
[#53]: https://github.com/iqm-finland/QDMI-on_IQM/pull/53
[#52]: https://github.com/iqm-finland/QDMI-on_IQM/pull/52
[#51]: https://github.com/iqm-finland/QDMI-on_IQM/pull/51
[#49]: https://github.com/iqm-finland/QDMI-on_IQM/pull/49
[#37]: https://github.com/iqm-finland/QDMI-on_IQM/pull/37

<!-- Contributor -->

[**@burgholzer**]: https://github.com/burgholzer
[**@marcelwa**]: https://github.com/marcelwa
[**@iqmtjm**]: https://github.com/iqmtjm

<!-- General links -->

[Keep a Changelog]: https://keepachangelog.com/en/1.1.0/
[Common Changelog]: https://common-changelog.org
[Semantic Versioning]: https://semver.org/spec/v2.0.0.html
