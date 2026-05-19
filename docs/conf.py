# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Sphinx configuration file."""

from __future__ import annotations

import os
from importlib import metadata

project = "QDMI on IQM"
author = "IQM Finland Oy"
language = "en"
project_copyright = "2025 - 2026 IQM Finland Oy"
master_doc = "index"

try:
    version = metadata.version("iqm-qdmi")
except metadata.PackageNotFoundError:
    version = "0.0.0"
release = version.split("+")[0]

extensions = [
    "myst_nb",
    "autoapi.extension",
    "sphinx.ext.autodoc",
    "sphinx.ext.intersphinx",
    "sphinx.ext.napoleon",
    "sphinx_copybutton",
    "sphinx_design",
    "sphinx.ext.viewcode",
    "breathe",
]

source_suffix = [".rst", ".md"]
exclude_patterns = [
    "_build",
    "**.ipynb_checkpoints",
    "**.jupyter_cache",
    "**.jupyter_execute",
    "Thumbs.db",
    ".DS_Store",
    ".env",
    ".venv",
]

templates_path = ["_templates"]
html_theme = "furo"
html_static_path = ["_static"]
html_css_files = [
    "custom.css",
    "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/fontawesome.min.css",
    "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/solid.min.css",
    "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/brands.min.css",
]
html_theme_options = {
    "light_logo": "iqm_logo.svg",
    "dark_logo": "iqm_logo_dark.svg",
    "source_repository": "https://github.com/iqm-finland/QDMI-on-IQM/",
    "source_branch": "main",
    "source_directory": "docs/",
    "navigation_with_keys": True,
    "footer_icons": [
        {
            "name": "GitHub",
            "url": "https://github.com/iqm-finland/QDMI-on-IQM/",
            "html": "",
            "class": "fa-brands fa-solid fa-github fa-2x",
        },
        {
            "name": "PyPI",
            "url": "https://pypi.org/project/iqm-qdmi/",
            "html": "",
            "class": "fa-brands fa-solid fa-python fa-2x",
        },
    ],
}

intersphinx_mapping = {
    "python": ("https://docs.python.org/3", None),
    "numpy": ("https://numpy.org/doc/stable/", None),
    "qiskit": ("https://docs.quantum.ibm.com/api/qiskit", None),
    "core": ("https://mqt.readthedocs.io/projects/core/en/latest", None),
}

myst_enable_extensions = [
    "amsmath",
    "colon_fence",
    "substitution",
    "deflist",
    "dollarmath",
]
myst_substitutions = {
    "version": version,
}
myst_heading_anchors = 3

nb_execution_mode = "cache"
nb_execution_raise_on_error = True
nb_execution_timeout = 60
nb_mime_priority_overrides = [("latex", "image/svg+xml", 15)]

copybutton_prompt_text = r"(?:\(\.?venv\) )?(?:\[.*\] )?\$ "
copybutton_prompt_is_regexp = True
copybutton_line_continuation_character = "\\"

modindex_common_prefix = ["iqm.qdmi."]
autoapi_dirs = ["../python/iqm"]
autoapi_python_use_implicit_namespaces = True
autoapi_root = "api"
autoapi_add_toctree_entry = False
autoapi_ignore = [
    "*/**/_version.py",
]
autoapi_options = [
    "members",
    "imported-members",
    "show-inheritance",
    "special-members",
    "undoc-members",
]
autoapi_keep_files = True
add_module_names = False
toc_object_entries_show_parents = "hide"
python_use_unqualified_type_names = True
napoleon_google_docstring = True
napoleon_numpy_docstring = False

breathe_projects = {"QDMI on IQM": "_build/doxygen/xml"}
breathe_default_project = "QDMI on IQM"

# get SKBUILD_BUILD_DIR from the environment as the stem for the documentation
try:
    skbuild_build_dir = os.environ["SKBUILD_BUILD_DIR"]
except KeyError:
    msg = "No build dir set in environment. Consider setting SKBUILD_BUILD_DIR."
    raise RuntimeError(msg) from None

breathe_projects_source = {
    "QDMI on IQM": (f"../{skbuild_build_dir}/src/include/iqm_qdmi", ["constants.h", "types.h", "export.h", "device.h"])
}
