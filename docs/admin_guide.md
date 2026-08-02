# Administrator Guide: Standing Up IQM Access on a Slurm Cluster

This tutorial walks a system administrator through standing up IQM quantum
computer (QC) access on a Slurm cluster from scratch, using the SPANK plugin and
a dedicated `quantum` partition. It cross-links the
[SPANK Plugin Guide](spank_plugin.md) and the [Spack Guide](spack_guide.md)
rather than repeating their reference material — use this page for the
end-to-end sequence, and follow the links for full option/flag detail. See
[Integration Scenarios Analysis](integration_scenarios.md) if you have not yet
decided this is the right integration path for your site.

## 1. Choose an Install Path

You have two ways to get the plugin and library onto your cluster:

- **Build from source** with CMake, covered in step 2 below.
- **Install via Spack**, if your site already manages software through Spack
  environments — see the [Spack Guide](spack_guide.md) for the package
  definition and `spack install` sequence. Spack changes only how the binaries
  land on disk, not the Slurm-side configuration in steps 3-4 below, which still
  applies either way.

## 2. Build and Install the SPANK Plugin

The plugin is tied to your cluster's Slurm daemon ABI, so build it on (or
against headers matching) the target environment, not a generic build host:

```bash
cmake -S . -B build-spank -DBUILD_IQM_SPANK=ON
cmake --build build-spank --target iqm-spank-plugin --parallel
sudo cmake --install build-spank --component iqm-spank-plugin
```

This requires Slurm 20.02 or newer, Slurm development headers (`slurm/spank.h`),
and a C++20-capable compiler (GCC 13+ or Clang 16+) — see
[Compatibility and Requirements](spank_plugin.md#compatibility-and-requirements)
for the full list, including the additional Slurm 23.02+ requirement if you plan
to enable license-based concurrency limits in step 4.

The install step places the compiled `iqm-spank-plugin.so` in the Slurm plugin
directory and drops a template configuration file into `plugstack.conf.d/`.
Deploy it to every login node (so `srun`/`sbatch`/`salloc` parse the `--iqm-*`
flags) and every compute node running `slurmd`/`slurmstepd` (so job steps get
the injected environment). Controller-only nodes do not need it.

Rebuild and redeploy the plugin after any major or minor Slurm upgrade, since it
links against your cluster's exact Slurm headers.

## 3. Configure `plugstack.conf`

Edit (or create) a drop-in file, e.g.
`/etc/slurm/plugstack.conf.d/iqm-qdmi.conf`, as a single line — `plugstack.conf`
does not support line continuation:

```text
required /usr/lib/slurm/iqm-spank-plugin.so iqm_base_url=https://resonance.iqm.tech iqm_tokens_file=/etc/iqm/tokens.json partitions=quantum
```

- `iqm_base_url` is the default IQM service endpoint for jobs that don't
  override it.
- `iqm_tokens_file` is a shared token file readable by `slurmd` on the compute
  nodes — see step 4 for why this, not a bare token, is the site-wide default.
- `partitions` restricts the plugin to the partitions you list (comma-
  separated); omit it and the plugin evaluates every partition, which is rarely
  what you want on a shared cluster.

Make sure your main `/etc/slurm/plugstack.conf` includes the drop-in directory:

```text
include /etc/slurm/plugstack.conf.d/*.conf
```

Then apply the change cluster-wide:

```bash
sudo scontrol reconfigure
```

Provision the `quantum` partition itself (or whatever name you chose) as a
regular Slurm partition gating the nodes with QC access — the
{py:mod}`~iqm.qdmi.offloader` module submits jobs to a partition named `quantum`
by default, so match that name unless your users configure otherwise. See the
full option reference, including `iqm_validation_timeout`, `iqm_license_prefix`,
and `iqm_require_license`, in
[Configuration](spank_plugin.md#configuration).

## 4. Set Up Authentication

Two mutually exclusive authentication modes are available; the plugin rejects a
configuration that sets both:

- `IQM_TOKENS_FILE` (recommended for site-wide defaults): a path to a token
  file, readable by `slurmd` on every compute node that needs it. This is the
  only supported way to set credentials as a cluster-wide default, because
  `IQM_TOKEN` passed directly on the command line would leak into shell history,
  process listings, and Slurm accounting records — see
  [Credential Security](spank_plugin.md#credential-security).
- `IQM_TOKEN`: acceptable for a user's own environment variable, not for an
  administrator-set `plugstack.conf` default.

Whichever you choose, the plugin performs a readability check on the tokens file
on each compute node before any task starts, as part of its mandatory per-node
launch-time validation.

## 5. Verify the Install

Confirm the configuration took effect:

```bash
scontrol show config | grep PlugStackConfig
```

This should point at your `plugstack.conf.d/` directory. Then submit a real test
job:

```bash
srun --partition=quantum --iqm-qc-alias=emerald python -c "from iqm.qdmi.qiskit import IQMBackend; print(IQMBackend().name)"
```

A successful launch prints a diagnostic summary line to `slurmd.log` on the
compute node:

```text
[iqm_spank_plugin] job=12345 partition=quantum base_url=set auth=tokens_file tokens_file_ok=yes
```

If you are developing or testing the plugin itself rather than verifying a
production install, you can instead run its test suite in an isolated Docker
container without touching real Slurm — see
[Testing with Docker](spank_plugin.md#testing-with-docker).

## 6. Troubleshooting

Read the diagnostic line's fields to localize a failure:

| Symptom                                              | Likely cause                                                                          | Fix                                                                                                   |
| :--------------------------------------------------- | :------------------------------------------------------------------------------------ | :---------------------------------------------------------------------------------------------------- |
| `base_url=unset`                                     | `iqm_base_url` missing from `plugstack.conf` and not set by the user                  | Set `iqm_base_url` in the drop-in config, or have the user pass `--iqm-base-url`                      |
| `auth=unset`                                         | Neither `IQM_TOKEN` nor `IQM_TOKENS_FILE` reached the job                             | Set `iqm_tokens_file` in `plugstack.conf`, or have the user pass `--iqm-tokens-file`                  |
| Job rejected for setting both token forms            | `IQM_TOKEN` and `IQM_TOKENS_FILE` both present                                        | Pick one; the plugin treats using both as a configuration conflict                                    |
| `tokens_file_ok=no`                                  | Token file missing or unreadable by `slurmd` on that compute node                     | Fix file permissions/path; the file must be readable on every compute node, not just the login node   |
| Options/flags not recognized (`--iqm-*` rejected)    | `plugstack.conf` drop-in directory not included, or not read-permitted                | Verify `scontrol show config \| grep PlugStackConfig`, and check file permissions on the drop-in file |
| "Plugin metadata symbol missing" at `slurmd` startup | Plugin built against incompatible Slurm headers                                       | Rebuild the plugin from source against this cluster's exact `slurm/spank.h`                           |
| "IQM backend validation failed"                      | Compute node can't reach `IQM_BASE_URL`, credentials invalid, or the QC doesn't exist | Check network egress from compute nodes, credential validity, and the QC id/alias spelling            |

For the full list of `plugstack.conf` options (including optional Slurm
license-based concurrency limits) and additional troubleshooting detail, see
[Troubleshooting](spank_plugin.md#troubleshooting) and
[Limiting Concurrent Access with Slurm Licenses](spank_plugin.md#limiting-concurrent-access-with-slurm-licenses)
in the SPANK Plugin Guide.

## 7. Hand Off to Users

Once a test job succeeds, point your users at:

- [Python Package](python_package.md) for the `iqm.qdmi.offloader` module and
  `iqm-sampler`/`iqm-estimator` CLI scripts.
- [Qiskit Integration](qiskit.md) for writing and running circuits directly
  against {py:class}`~iqm.qdmi.qiskit.IQMBackend`.
- The [SPANK Plugin Guide](spank_plugin.md#for-users)'s user-facing section for
  the `--iqm-*` `srun`/`sbatch`/`salloc` flags themselves.
