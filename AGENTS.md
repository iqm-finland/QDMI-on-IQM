# QDMI-on-IQM

## C++

- Configure:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_QDMI_TESTS=ON`
- Build: `cmake --build build --config Release`
- Run unit tests without IQM access:
  `ctest -C Release --test-dir build/test/unit --output-on-failure`
- Run a single unit test binary: `./build/test/unit/unit_tests`
- Run all configured tests:
  `ctest -C Release --test-dir build --output-on-failure`
- For debug builds, replace `Release` with `Debug`.

Integration tests under `test/integration/` require access to an IQM backend.
Set the required `IQM_*` environment variables and run them only when live IQM
access is explicitly intended.

## Python

- Run a single supported Python version during development:
  `uvx nox -s tests-3.14`
- Run the full supported-version matrix: `uvx nox -s tests`
- Run minimum-dependency tests: `uvx nox -s minimums-3.14`
- Run end-to-end examples on the simulator:
  `uvx nox -s examples -- --backend sim`
- Run a targeted test: `uvx nox -s tests-3.14 -- -k <test-expression>`

The default examples session targets real IQM hardware. Do not run it without
explicit intent, valid credentials, and an appropriate target.

## Documentation

- Sources: `docs/`
- Build documentation: `uvx nox -s docs`
- Check links: `uvx nox -s docs -- -b linkcheck`
- Generated HTML: `docs/_build/html/`

## SPANK Plugin

- Sources: `spank/`
- Build with the core project by configuring CMake with `-DBUILD_IQM_SPANK=ON`
  on a Linux system with Slurm development headers.
- The SPANK plugin is GPLv3-licensed, unlike the Apache-2.0-with-LLVM-exception
  core project. Preserve this license boundary and the applicable file headers.
- Smoke and Resonance sessions in `spank/noxfile.py` require an active Slurm
  deployment; Resonance tests additionally require IQM credentials. Do not run
  either session unless that external access is explicitly intended.

## Tech Stack

### General

- `prek` for pre-commit hooks
- `uv` for Python dependency and environment management
- `nox` for test, lint, examples, and documentation automation

### C++

- C++20
- CMake 3.24+
- `FetchContent` for dependency management in `cmake/ExternalDependencies.cmake`
- GoogleTest for tests in `test/unit/` and `test/integration/`
- `clang-format` and `clang-tidy` configured by `.clang-format` and
  `.clang-tidy`

### Python

- Python 3.10+
- `scikit-build-core` as the build backend
- `pytest` for tests in `test/python/`
- `ruff` for formatting and linting
- `ty` for type checking

### Documentation

- Sphinx and MyST
- Furo theme
- Doxygen and Breathe for C++ API documentation
- AutoAPI for Python API documentation

## Development Guidelines

### General

- MUST read and follow `docs/contributing.md`.
- MUST run `uvx nox -s lint` after every batch of changes. This runs the full
  `prek` hook set from `.pre-commit-config.yaml`, including formatting, linting,
  spelling, license-header, metadata, lockfile, and type checks.
- MUST add or update tests for every code change, including regression tests for
  bug fixes.
- MUST keep changes focused and avoid unrelated refactors or formatting.
- MUST follow existing patterns in neighboring source and test files.
- MUST document new user-facing behavior and update `CHANGELOG.md` for
  noteworthy user-facing changes.
- MUST fold additions, enhancements, or bug fixes for features that are still in
  `[Unreleased]` into the existing `[Unreleased]` `CHANGELOG.md` entry for that
  feature (appending the PR number) rather than creating separate bullet points
  for every minor addition to an unreleased feature.
- MUST update `docs/examples.md` when changing an example.
- MUST preserve the existing license header and SPDX identifier appropriate to
  the file's component.
- MUST include a commit footer attribution in the form
  `Assisted-by: [Model Name] via [Tool Name]` if AI tools are used to prepare a
  commit. For example: `Assisted-by: gpt-5.6-sol via Codex CLI`.
- NEVER expose, print, store, or commit IQM tokens or token-file contents.
- NEVER run live IQM, Resonance, or Slurm integration workflows unless the task
  explicitly requires them.
- PREFER targeted tests during development before running broader checks.

### GitHub Pull Requests

- Agents MAY create, submit, and edit pull requests and MAY comment on pull
  requests when doing so is part of the requested task.
- When the repository provides a GitHub pull request template, agents MUST use
  and follow that template when creating or editing a pull request.
- Every agent-authored or agent-edited text submitted to a pull request MUST
  begin with the following disclaimer:

  `🤖 *AI text below* 🤖`
- This requirement applies to pull request descriptions, review bodies, inline
  review comments, issue-style comments, replies, and other agent-produced text
  bodies. Pull request titles are exempt and MUST NOT include the disclaimer.
- Put the disclaimer on the first line of every applicable text body.
- When editing existing human-authored text, preserve the original content and
  add the disclaimer at the very beginning of the edited field.
- The disclaimer MUST be visible in the submitted text; do not hide it in an
  HTML comment, metadata field, attachment, or linked document.

### C++

- MUST use Doxygen-style comments for public APIs and new documented code.
- MUST follow the LLVM-based style configured in `.clang-format` and the naming
  conventions in `.clang-tidy`.
- MUST add unit tests under `test/unit/` when behavior can be tested without a
  live backend.
- PREFER C++20 standard-library features over custom implementations.
- PREFER hermetic HTTP stubs and fixtures over network-dependent tests.

### Python

- MUST use Google-style docstrings.
- MUST add tests under `test/python/` for Python behavior.
- PREFER running one Python version locally before the full version matrix.
- PREFER fixing `ruff` and `ty` findings over adding suppression comments;
  document suppressions that are genuinely necessary.

## Self-Review Checklist

- Did `uvx nox -s lint` pass without errors?
- Are all behavior changes covered by automated tests?
- Did the relevant C++ unit or Python tests pass?
- Were documentation and `CHANGELOG.md` updated where appropriate?
- Were example documentation and component-specific license headers preserved?
- Were live integration tests avoided, or explicitly authorized and reported?
