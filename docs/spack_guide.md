# Spack Package Guide

This project can be built as a [Spack](https://spack.io/) package. This guide provides instructions on how to set up the necessary
Spack configuration.

## Installation Steps

1.  Ensure you have Spack installed and set up.
2.  Create a directory named `iqm-qdmi` in your Spack package repository.
3.  Create a file named `package.py` in that directory with the following content.

```python
# Copyright 2013-2024 Lawrence Livermore National Security, LLC and other
# Spack Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

from spack.package import *


class IqmQdmi(CMakePackage):
    """IQM QDMI Device."""

    homepage = "https://github.com/iqm-finland/QDMI-on-IQM"
    git = "git@github.com:iqm-finland/QDMI-on-IQM.git"

    maintainers("@burgholzer", "@marcelwa")

    version("main", branch="main")

    depends_on("cmake", type="build")
    depends_on("curl", type=("build", "link", "run"))
    depends_on("nlohmann-json")


    def cmake_args(self):
        args = [
            self.define("BUILD_IQM_QDMI_TESTS", False)
        ]
        return args
```

4. Run `spack spec iqm-qdmi@main` to grab the specifications for the `iqm-qdmi` package.
5. If you don't have your environment set up with the necessary build tools, run `spack compiler find` and
   `spack external find gmake ncurses cmake git`. Then go back to step 4.
6. Run `spack install iqm-qdmi@main` to install the package.

When this repo offers a release version via git tag, more versioning information can be added to `package.py` as well.
Currently, the only option is to fetch directly from `main`. In the future, a setup like
`version("1.0", sha256="<commit hash>")` allows for more fine-grained control over which commit to fetch when building.
