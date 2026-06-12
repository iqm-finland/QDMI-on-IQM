# IQM QDMI SPANK Plugin

[![SPANK Plugin GPLv3 License](https://img.shields.io/static/v1?logo=gnu&label=License&message=GPLv3&color=informational&style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![C++20](https://img.shields.io/static/v1?logo=cplusplus&label=C%2B%2B&message=20&color=informational&style=flat-square)](https://isocpp.org/)
[![Slurm SPANK](https://img.shields.io/static/v1?label=Slurm&message=SPANK&color=informational&style=flat-square)](https://slurm.schedmd.com/spank.html)

This plugin integrates the QDMI-on-IQM runtime with Slurm using the [SPANK plugin interface](https://slurm.schedmd.com/spank.html). It allows Slurm to automatically inject the required `IQM_*` environment variables into jobs and registers corresponding `--iqm-*` command-line options for `srun`, `sbatch`, and `salloc`.

For details on building, installing, configuring, and using the plugin, please refer to the main [SPANK Plugin Guide](../docs/spank_plugin.md).

## License and Compatibility

The SPANK plugin in this directory is licensed under GPLv3 because it links against Slurm's GPL-licensed SPANK interface. See [spank/LICENSE.md](LICENSE.md) for the plugin license text.

Unlike the core QDMI-on-IQM library, this plugin is tied directly to the Slurm ABI of the target system. It must be compiled against the target cluster's Slurm header files (`slurm/spank.h`) and rebuilt whenever the cluster's Slurm version is upgraded. For detailed version compatibility and compiler requirements, see the [SPANK Plugin Guide](../docs/spank_plugin.md).
