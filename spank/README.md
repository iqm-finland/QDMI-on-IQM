# IQM QDMI SPANK Plugin

[![SPANK Plugin GPLv3 License](https://img.shields.io/static/v1?logo=gnu&label=License&message=GPLv3&color=informational&style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![C++20](https://img.shields.io/static/v1?logo=cplusplus&label=C%2B%2B&message=20&color=informational&style=flat-square)](https://isocpp.org/)
[![Slurm SPANK](https://img.shields.io/static/v1?label=Slurm&message=SPANK&color=informational&style=flat-square)](https://slurm.schedmd.com/spank.html)

**IQM QDMI SPANK Plugin** integrates the QDMI-on-IQM runtime with Slurm through
the [SPANK plugin interface](https://slurm.schedmd.com/spank.html).
It injects the canonical `IQM_*` environment contract into Slurm jobs, registers
`--iqm-*` submission options, and supports partition-gated activation.

This subproject is the Slurm-facing companion to the main QDMI-on-IQM library.
It is intended for HPC administrators and platform integrators who need to make
IQM backends available through standard `srun` and `sbatch` workflows.

## Quick Start

The plugin makes your IQM configuration available directly inside Slurm jobs. You can use the new `--iqm-*` flags to target specific quantum backends:

```bash
# Run a Python job on the 'quantum' partition with a specific backend alias
srun --partition=quantum --iqm-qc-alias=emerald python my_circuit.py
```

Inside your script, the `IQMBackend` (from `iqm-qdmi`) will automatically pick up these settings—no manual `export` or hardcoded URLs required.

## Where to Start

| I want to…                                                | Guide                                         |
| :-------------------------------------------------------- | :-------------------------------------------- |
| Understand the plugin runtime contract and user workflows | [SPANK Plugin Guide](../docs/spank_plugin.md) |
| Configure plugstack entries and site defaults             | [SPANK Plugin Guide](../docs/spank_plugin.md) |
| Build the plugin together with QDMI-on-IQM                | [Root README](../README.md)                   |

## What the Plugin Does

The plugin is intentionally narrow in scope:

- Registers `--iqm-*` Slurm options and maps them to `IQM_*` environment variables.
- Applies site defaults from `plugstack.conf` without taking away per-job overrides.
- Restricts activation to configured partitions when `partitions=` is used.
- Performs shallow local checks such as required base URL presence and conflicting
  authentication sources.

Direct token passing is intentionally not supported by this plugin. Slurm
submission commands may be recorded in shell history, scheduler logs,
accounting data, audit trails, or process listings, so accepting a raw token would expose credentials in plain text. Use `--iqm-tokens-file`
instead, so the job receives only a path to a protected credentials file.

It does not implement scheduler policy, quantum resource brokering, or backend-side
job semantics. Those remain the responsibility of Slurm configuration and the
underlying IQM QDMI device implementation.

## Build and Installation

The plugin is built from the main repository root together with QDMI-on-IQM. For
the full build and installation flow, see [docs/spank_plugin.md](../docs/spank_plugin.md).

At build time, this component depends on site-provided Slurm development headers.
Because SPANK plugins are compiled against the target Slurm installation, they are
typically deployed as cluster-specific artifacts rather than treated as universally
portable binaries.

Build against the target cluster's Slurm headers and rebuild after Slurm
major-version upgrades.

## License and Compatibility

The SPANK plugin in this directory is licensed under GPLv3 because it links against
Slurm's GPL-licensed SPANK interface. See [spank/LICENSE.md](LICENSE.md) for the
plugin license text.

This plugin also has a different compatibility boundary from the rest of
QDMI-on-IQM: while the core library follows the repository's QDMI-on-IQM release
versioning, the SPANK plugin must be built against the Slurm headers of the target
system. In practice, that means the effective plugin variant is tied to the Slurm
version and header set available on the cluster where it is compiled.

For detailed information on Slurm version compatibility and toolchain requirements, see the [SPANK Plugin Guide](../docs/spank_plugin.md).
