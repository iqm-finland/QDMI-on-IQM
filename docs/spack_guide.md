# Integrating with Spack

This project can be built as a [Spack](https://spack.io/) package. This guide
explains how to create a local Spack package definition for `iqm-qdmi` and
install it in your Spack environment.

## Prerequisites

- Spack is installed and initialized (see the
  [Spack Getting Started guide](https://spack.readthedocs.io/en/latest/getting_started.html)).
- Your compiler and basic build tools (`cmake`, `git`, `make`) are discoverable
  by Spack. If they are not, run:

  ```console
  spack compiler find
  spack external find gmake ncurses cmake git
  ```

## Creating the Package Definition

Spack packages live in a _package repository_. You can add the definition to an
existing custom repo or create a dedicated one.

1. Locate or create a Spack package repository directory, e.g.
   `~/spack-packages`. If you are creating a fresh one, initialize it:

   ```console
   spack repo create ~/spack-packages
   spack repo add ~/spack-packages
   ```

2. Create the package directory and file:

   ```console
   mkdir -p ~/spack-packages/packages/iqm-qdmi
   touch ~/spack-packages/packages/iqm-qdmi/package.py
   ```

3. Paste the following content into `package.py`:

   Replace each placeholder with a concrete value before installing the package.
   Use your desired version for `<version>` as `X.Y.Z`, the matching Git tag for
   `<tag>`, and the full commit SHA for that tag as `<commit>`.

   ```python
   # Copyright 2013-2024 Lawrence Livermore National Security, LLC and other
   # Spack Project Developers. See the top-level COPYRIGHT file for details.
   #
   # SPDX-License-Identifier: (Apache-2.0 OR MIT)

   from spack.package import *


   class IqmQdmi(CMakePackage):
       """IQM QDMI Device — IQM's implementation of the Quantum Device Management Interface."""

       homepage = "https://github.com/iqm-finland/QDMI-on-IQM"
       git = "https://github.com/iqm-finland/QDMI-on-IQM.git"

       maintainers("@burgholzer", "@marcelwa")

       version("<version>", tag="<tag>", commit="<commit>")
       version("main", branch="main")

       depends_on("cmake", type="build")
       depends_on("curl", type=("build", "link", "run"))
       depends_on("nlohmann-json")

       def cmake_args(self):
           return [self.define("BUILD_IQM_QDMI_TESTS", False)]
   ```

## Installing a Stable Release

To install a stable release, replace `<version>` with a concrete release and
run:

```console
spack spec iqm-qdmi@<version>
spack install iqm-qdmi@<version>
```

`spack spec` performs a dry-run concretization, so you can verify the dependency
tree before committing to the installation.

## Installing from `main`

If you need the very latest development snapshot, you can install directly from
the `main` branch instead:

```console
spack spec iqm-qdmi@main
spack install iqm-qdmi@main
```

:::{note}
`@main` tracks a moving target and may include unreleased or unstable changes.
Prefer a stable version for production use.
:::

## Using the Installed Package

After installation, load the package into your environment with:

```console
spack load iqm-qdmi@<version-or-branch>
```

Downstream CMake projects can then find the library via
`find_package(iqm-qdmi-device)`. The installed headers and the shared library
path are also exposed through the `iqm-qdmi` Python entry point if you have the
Python package installed alongside (see [Python Package](python_package.md)).

## Adding Future Versions

When a new release tag is published, add a corresponding `version()` line to
`package.py` with the new tag name and its commit SHA:

```python
version("X.Y.Z", tag="vX.Y.Z", commit="<full commit SHA of the tag>")
```

The `commit` parameter pins the exact source revision and guarantees
reproducible builds even if the tag is later moved.
