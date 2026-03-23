# Contributing

Thank you for your interest in contributing to the IQM QDMI Device implementation! This document outlines how to contribute and the development guidelines.

We use GitHub to [host code](https://github.com/iqm-finland/QDMI), to [track issues and feature requests](https://github.com/iqm-finland/QDMI/issues), as well as accept [pull requests](https://github.com/iqm-finland/QDMI/pulls). See <https://docs.github.com/en/get-started/quickstart> for a general introduction to working with GitHub and contributing to projects.

## Types of Contributions

Pick the path that fits your time and interests:

- 🐛 **Report bugs**:
  Use the _🐛 Bug report_ template at <https://github.com/iqm-finland/QDMI/issues>.
  Include steps to reproduce, expected vs. actual behavior, environment details, and a minimal example.

- 🛠️ **Fix bugs**:
  Browse [issues](https://github.com/iqm-finland/QDMI/issues), especially those labeled "bug", "help wanted", or "good first issue".
  Open a draft PR early to get feedback.

- 💡 **Propose features**:
  Use the _✨ Feature request_ template at <https://github.com/iqm-finland/QDMI/issues>.
  Describe the motivation, alternatives considered, and (optionally) a small API sketch.

- ✨ **Implement features**:
  Pick items labeled "feature" or "enhancement".
  Coordinate in the issue first if the change is substantial; start with a draft PR.

- 📝 **Improve documentation**:
  Add or refine docstrings, tutorials, and examples; fix typos; clarify explanations.
  Small documentation-only PRs are very welcome.

- ⚡️ **Performance and reliability**:
  Profile hot paths, add benchmarks, reduce allocations, deflake tests, and improve error messages.

- 📦 **Packaging and tooling**:
  Improve build configuration, CI workflows, and platform support.
  Incremental tooling fixes have a big impact.

- 🙌 **Community support**:
  Triage issues, reproduce reports, and answer questions in [Discussions](https://github.com/iqm-finland/QDMI/discussions).

## Guidelines

Please adhere to the following guidelines to help the project grow sustainably.

### Core Guidelines

- ["Commit early and push often"](https://www.worklytics.co/blog/commit-early-push-often).
- Write meaningful commit messages, preferably using [gitmoji](https://gitmoji.dev) for additional context.
- Focus on a single feature or bug at a time and only touch relevant files.
  Split multiple features into separate contributions.
- Add tests for new features to ensure they work as intended.
- Document new features.
  For user-facing changes, consider adding documentation updates to the relevant guides.
- Add tests for bug fixes to demonstrate the fix.
- Document your code thoroughly and ensure it is readable.
- Keep your code clean by removing debug statements, leftover comments, and unrelated code.
- Check your code for style and linting errors before committing.
- Follow the project's coding standards and conventions.
- Be open to feedback and willing to make necessary changes based on code reviews.

### Pull Request Workflow

- Create PRs early.
  Work-in-progress PRs are welcome; mark them as drafts on GitHub.
- Use a clear title, reference related issues by number, and describe the changes.
  Follow the PR template; only omit the issue reference if not applicable.
- CI runs on all supported platforms to build, test, format, and lint.
  All checks must pass before merging.
- When ready, convert the draft to a regular PR and request a review from a maintainer.
  If unsure, ask in PR comments.
  If you are a first-time contributor, mention a maintainer in a comment to request a review.
- If your PR gets a "Changes requested" review, address the feedback and push updates to the same branch.
  Do not close and reopen a new PR.
  Respond to comments to signal that you have addressed the feedback.
  Do not resolve review comments yourself; the reviewer will do so once satisfied.
- Re-request a review after pushing changes that address feedback.
- Do not squash commits locally; maintainers typically squash on merge.
  Avoid rebasing or force-pushing before reviews; you may rebase after addressing feedback if desired.

## Get Started 🎉

Ready to contribute? We value contributions from people with all levels of experience. In particular, if this is your first PR, not everything has to be perfect. We will guide you through the process.

## Installation

### Initial Setup

1. **Get the code**

   **External Contribution**

   If you do not have write access to the [iqm-finland/QDMI](https://github.com/iqm-finland/QDMI) repository, fork the repository on GitHub (see <https://docs.github.com/en/get-started/quickstart/fork-a-repo>) and clone your fork locally.

   ```console
   $ git clone git@github.com:your_name_here/QDMI.git
   ```

   **Internal Contribution**

   If you do have write access to the [iqm-finland/QDMI](https://github.com/iqm-finland/QDMI) repository, clone the repository locally.

   ```console
   $ git clone git@github.com:iqm-finland/QDMI.git
   ```

2. **Change into the project directory**

   ```console
   $ cd QDMI
   ```

3. **Create a branch for local development**

   ```console
   $ git checkout -b name-of-your-bugfix-or-feature
   ```

   Now you can make your changes locally.

4. **Install pre-commit hooks** (recommended)

   The project uses [pre-commit](https://pre-commit.com/) hooks for running linters and formatting tools on each commit. These checks can be run automatically on every commit via [prek](https://prek.j178.dev) (recommended).

   To set this up, install prek, e.g., via:

   **macOS and Linux:**

   ```console
   $ curl --proto '=https' --tlsv1.2 -LsSf https://github.com/j178/prek/releases/latest/download/prek-installer.sh | sh
   ```

   **Windows:**

   ```console
   $ powershell -ExecutionPolicy ByPass -c "irm https://github.com/j178/prek/releases/latest/download/prek-installer.ps1 | iex"
   ```

   **Using uv:**

   ```console
   $ uv tool install prek
   ```

   Then run:

   ```console
   $ prek install
   ```

## Working on the C++ Library

Building the project requires a C++20-capable C++ compiler and [CMake](https://cmake.org/) 3.24 or newer.

### Configure and Build

This project uses CMake as the main build configuration tool. Building a project using CMake is a two-stage process. First, CMake needs to be _configured_ by calling:

```console
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_QDMI_TESTS=ON
```

This tells CMake to:

- search the current directory `.` (passed via `-S`) for a `CMakeLists.txt` file,
- process it into a directory `build` (passed via `-B`),
- configure a `Release` build (passed via `-DCMAKE_BUILD_TYPE`) as opposed to, e.g., a `Debug` build, and
- enable building the tests (passed via `-DBUILD_IQM_QDMI_TESTS=ON`).

Additional useful flags:

- `-DIQM_QDMI_COVERAGE=ON` to enable code coverage support
- `-DBUILD_IQM_QDMI_DOCS=ON` to enable building the documentation

After configuring with CMake, the project can be built by calling:

```console
$ cmake --build build --config Release
```

This builds the project in the `build` directory (passed via `--build`). Some operating systems and development environments explicitly require a configuration to be set, which is why the `--config` flag is also passed to the build command. The flag `--parallel <NUMBER_OF_THREADS>` may be added to trigger a parallel build.

Building the project this way generates:

- the main project libraries in the `build/src` directory and
- some test executables in the `build/test` directory.

:::{note}
This project uses CMake's [`FetchContent`](https://cmake.org/cmake/help/latest/module/FetchContent.html) module to download and build its dependencies. The first time you configure the project, you will need an active internet connection to fetch the required libraries. Subsequent builds will use the cached dependencies.
:::

### Running the C++ Tests and Code Coverage

We use the [GoogleTest](https://google.github.io/googletest/primer.html) framework for unit testing of the C++ library. All tests are contained in the `test` directory, which is further divided into:

- **Unit Tests** (`test/unit/`): Self-contained tests that do not require external dependencies or a live connection to IQM hardware.
- **Integration Tests** (`test/integration/`): Tests that require a live connection to an IQM backend.

You are expected to write tests for any new features you implement and ensure that all tests pass. Our CI pipeline on GitHub will also run the tests and check for any failures. It will also collect code coverage information and upload it to Codecov. Our goal is to have new contributions at least maintain the current code coverage level, while striving for covering as much of the code as possible. Try to write meaningful tests that actually test the correctness of the code and not just exercise the code paths.

To run the tests, use CMake's test runner [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html):

**Running all tests:**

```console
$ ctest -C Release --test-dir build --output-on-failure
```

**Running only unit tests (no hardware required):**

```console
$ ctest -C Release --test-dir build/test/unit --output-on-failure
```

**Running integration tests (requires IQM access):**

Before running the integration tests, make sure you have set the necessary environment variables:

```console
$ export IQM_BASE_URL="https://desired-iqm-server.com"
$ export RESONANCE_API_KEY="your-api-key"
$ ctest -C Release --test-dir build/test/integration --output-on-failure
```

**Generating code coverage:**

To generate a code coverage report, configure the project with `-DIQM_QDMI_COVERAGE=ON`, build, run the tests, and then use `lcov`:

```console
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_IQM_QDMI_TESTS=ON -DIQM_QDMI_COVERAGE=ON
$ cmake --build build
$ ctest --test-dir build --output-on-failure
$ lcov --capture --directory build --output-file coverage.info
$ lcov --remove coverage.info '/usr/*' '*/test/*' '*/build/_deps/*' --output-file coverage.info
$ lcov --list coverage.info
```

### C++ Code Formatting and Linting

This project mostly follows the [LLVM Coding Standard](https://llvm.org/docs/CodingStandards.html), which is a set of guidelines for writing C++ code. To ensure the quality of the code and that it conforms to these guidelines, we use:

- [`clang-tidy`](https://clang.llvm.org/extra/clang-tidy/), a static analysis tool that checks for common mistakes in C++ code, and
- [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html), a tool that automatically formats C++ code according to a given style guide.

Common IDEs like [CLion](https://www.jetbrains.com/clion/) or [Visual Studio Code](https://code.visualstudio.com/) have plugins that can automatically run `clang-tidy` on the code and automatically format it with `clang-format`.

- If you are using CLion, you can configure the project to use the `.clang-tidy` and `.clang-format` files in the project root directory.
- If you are using Visual Studio Code, you can install the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).

They will automatically execute `clang-tidy` on your code and highlight any issues. In many cases, they also provide quick-fixes for these issues. Furthermore, they provide a command to automatically format your code according to the given style.

Our `pre-commit` configuration includes `clang-format`. If you have installed `prek`, it will automatically run `clang-format` on your code before each commit. If you do not have `prek` set up, the [pre-commit.ci](https://pre-commit.ci) bot will run `clang-format` on your code and automatically format it according to the style guide.

:::{tip}
Remember to pull the changes back into your local repository after the bot has formatted your code to avoid merge conflicts.
:::

Our CI pipeline will also run `clang-tidy` over the changes in your PR and report any issues it finds. Due to technical limitations, the workflow can only post PR comments if the changes are not coming from a fork. If you are working on a fork, you can still see the `clang-tidy` results in the GitHub Actions logs or on the workflow summary page.

## Working on the Documentation

The documentation is written in [Markdown](https://www.markdownguide.org/) and built using [Doxygen](https://www.doxygen.nl/index.html). The documentation source files can be found in the `docs/` directory.

### Writing Documentation

We expect any new additions to the code base to be documented using [Doxygen](https://www.doxygen.nl/index.html) comments. When touching existing code, we encourage you to add Doxygen comments to the code you touch or refactor.

For some tips on how to write good Doxygen comments, see the [Doxygen Manual](https://www.doxygen.nl/manual/docblocks.html).

Documentation improvements are highly valued! This includes:

- Adding or improving Doxygen comments in the C++ code
- Improving the user guides and tutorials in the `docs/` directory
- Adding examples and code snippets
- Fixing typos and improving clarity

### Building the Documentation

To build the documentation, you need to have Doxygen installed. Then, after configuring the project with CMake and the `-DBUILD_IQM_QDMI_DOCS=ON` flag, you can build the documentation by running:

```console
$ cmake --build build --target iqm_qdmi_device_docs
```

The documentation will be generated in the `build/docs` directory as both HTML and LaTeX. You can view the HTML documentation by opening the `index.html` file in a web browser.

## Tips for Development

If something goes wrong, the CI pipeline will notify you. Here are some tips for finding the cause of certain failures:

- If any of the **test checks** fail, this indicates build errors or test failures.
  Look through the respective logs on GitHub for any error or failure messages.

- If **codecov** checks fail, this means that your changes are not appropriately covered by tests or that the overall project coverage decreased too much.
  Ensure that you include tests for all your changes in the PR.

- If **cpp-linter** comments on your PR with a list of warnings, these have been raised by `clang-tidy` when checking the C++ part of your changes for warnings or style guideline violations.
  The individual messages frequently provide helpful suggestions on how to fix the warnings.

- If the **pre-commit.ci** check fails, some of the `pre-commit` checks failed and could not be fixed automatically by the pre-commit.ci bot.
  The individual log messages frequently provide helpful suggestions on how to fix the warnings.

- If the **documentation build** fails, the documentation could not be built properly.
  Inspect the corresponding log file for any errors.

## Security

If you discover a security vulnerability, please follow the guidelines in our [Security Policy](security.md) to report it responsibly.

## Questions?

If you have any questions about contributing, feel free to:

- Open an issue with the "question" label
- Start a discussion in [GitHub Discussions](https://github.com/iqm-finland/QDMI/discussions)
- Reach out to the maintainers directly

Thank you for contributing to the IQM QDMI Device implementation! 🎉
