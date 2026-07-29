# Integration Scenarios Analysis

This page compares the ways an HPC center or downstream project can integrate
IQM quantum computers (QCs) through QDMI-on-IQM. Each scenario links to the
guide that covers its mechanics; this page focuses on when to pick it, what it
costs to set up, and what isolation and multi-tenancy properties it gives you. A
recommendation matrix at the end maps site profiles to scenarios.

:::{note}
Scenarios 1-6 describe integration paths this repository implements today.
Scenario 7 is forward-looking: it discusses schedulers this repository does not
yet support, to help sites evaluating IQM integration outside Slurm.
:::

## 1. Direct C++ QDMI Device Usage

Link a downstream C++ application directly against the QDMI device library and
drive a session yourself — see the [Usage Guide](usage.md). No scheduler is
involved: the calling process owns the session lifecycle, authentication, and
calibration/job queries end to end.

- **Setup complexity**: low — link the library, call the C API.
- **Isolation**: whatever the host process provides; QDMI-on-IQM itself enforces
  no isolation between callers.
- **Multi-tenancy**: none built in. Concurrent callers targeting the same QC
  contend at the IQM service's own queue, with no cluster-side arbitration.
- **Resource brokering**: none — the QC's own queue is the only broker.

## 2. Direct Python/Qiskit Usage

Use {py:class}`~iqm.qdmi.qiskit.IQMBackend` directly from a Python process, as
shown in [Qiskit Integration](qiskit.md). Same properties as Scenario 1, but at
the Qiskit/Python layer: no scheduler, no cluster-side isolation, transpilation
handled by Qiskit's standard tools before the circuit reaches the QC.

- **Setup complexity**: low — `uv pip install iqm-qdmi[qiskit]`.
- **Isolation / multi-tenancy / resource brokering**: same as Scenario 1.
- **Fits**: single-researcher workstations, notebooks with direct network access
  to the IQM service, CI jobs that don't run on a cluster.

## 3. Slurm + SPANK Env-Injection with Synchronous `srun` Offloading

The production integration path today: the [SPANK plugin](spank_plugin.md)
injects `IQM_*` environment variables into job steps, and the
{py:mod}`~iqm.qdmi.offloader` module (see
[Python Package](python_package.md#programmatic-offloading-with-the-offloader-module))
submits `srun iqm-sampler`/`srun iqm-estimator` jobs from a login-node process
such as a Jupyter notebook.

- **Setup complexity**: moderate — compile and deploy the SPANK plugin on login
  and compute nodes, configure `plugstack.conf`, provision a `quantum` partition
  (see the [Administrator Guide](admin_guide.md)).
- **Isolation**: partition-gated — the plugin only activates on
  administrator-listed partitions, and per-node launch-time validation rejects a
  job step before any task starts if the target QC or credentials are invalid.
- **Multi-tenancy**: Slurm's own scheduler arbitrates access to the `quantum`
  partition; optionally, administrators can additionally model each QC as a
  Slurm license (see
  [Limiting Concurrent Access with Slurm Licenses](spank_plugin.md#limiting-concurrent-access-with-slurm-licenses))
  so Slurm itself caps concurrent QC access ahead of the QC's own queue — most
  useful for on-premise, effectively single-tenant hardware.
- **Resource brokering**: deliberately shallow. The plugin injects environment
  variables and validates reachability; it does not lock or schedule the QC
  itself. Precedence when the same setting is given multiple ways: `srun`
  command-line flags override user-set environment variables, which override
  `plugstack.conf` administrator defaults.

## 4. Spack-Based Install

Layer a Spack package definition (see the [Spack Guide](spack_guide.md)) on top
of any of the above scenarios as the install mechanism, instead of a manual
CMake build.

- **Setup complexity**: low once a package repository exists; concretization and
  reproducible pinning (via commit SHA) are handled by Spack.
- **Fits**: HPC centers that already manage their software stack through Spack
  environments/modules rather than ad hoc builds.
- Does not change the isolation, multi-tenancy, or resource-brokering properties
  of the underlying scenario (3 or 1) — it only changes how the binaries get
  onto the cluster.

## 5. Docker-Based Test/Dev Topology vs. Bare-Metal Production Deployment

The SPANK plugin's test suite runs inside Docker (`spank/Dockerfile`,
`.github/workflows/spank-tests.yml`) without requiring a real Slurm installation
or credentials, as described in
[Testing with Docker](spank_plugin.md#testing-with-docker).
Production deployment instead installs the compiled plugin directly onto real
login and compute nodes running `slurmd`/`slurmstepd`.

- **Fits**: use the Docker topology for plugin development and CI; use the
  bare-metal topology for any environment where jobs actually reach IQM
  hardware, since the plugin is tied to the target cluster's Slurm daemon ABI
  and must be rebuilt against it (see
  [Compatibility and Requirements](spank_plugin.md#compatibility-and-requirements)).
- These two topologies are not alternatives for the same purpose — treat Docker
  as a pre-production verification step, not a deployment option.

## 6. Shared Multi-Tenant Cluster vs. Dedicated/Single-User Access

Orthogonal to Scenarios 1-5: the same integration mechanism behaves differently
depending on who else is on the cluster.

- **Shared multi-tenant cluster**: partition gating (Scenario 3) and,
  optionally, Slurm licenses become load-bearing — without them, any user on any
  partition could target the QC, and concurrent jobs could overwhelm an
  on-premise QC's own queue.
- **Dedicated/single-user cluster**: partition gating and license limits are
  still supported but less critical, since there is no competing tenant to
  isolate from.
- Direct usage (Scenarios 1-2) has no partition/license concept at all — choose
  Scenario 3 instead as soon as more than one user or job needs regulated access
  to the same QC.

## 7. Forward-Looking: Non-Slurm Schedulers (Not Implemented)

QDMI-on-IQM only supports Slurm today. Sites running other schedulers would need
an equivalent env-injection mechanism, and the SPANK plugin's "shallow, no
resource brokering" design is itself Slurm-specific — it would not port to
another scheduler unchanged:

- **PBS/OpenPBS**: would need a PBS hook (`qmgr` server/queue hooks) written
  against PBS's own hook API to inject `IQM_*` variables into the job
  environment at launch — a different lifecycle and API from SPANK's.
- **LSF**: would need an `esub`/`eexec` job submission wrapper or an LSF
  external scheduler plugin; LSF has no direct SPANK equivalent, so environment
  injection and any launch-time validation would need to be reimplemented
  against LSF's job-control hooks.
- **Kubernetes**: would look structurally different from all of the above —
  likely a mutating admission webhook or device-plugin-style resource
  advertisement, since Kubernetes has no per-job-step CLI flag-parsing hook
  comparable to SPANK; partition gating's closest analogue would be namespace-
  or `ResourceQuota`-scoped access.

None of these are implemented in this repository. Treat this section as scoping
input for a site evaluating IQM integration outside Slurm, not as supported
functionality.

## Recommendation Matrix

| Site profile | Recommended scenario |
| :--- | :--- |
| Single researcher, workstation or notebook, direct network access to the IQM service | 1 (C++) or 2 (Python/Qiskit) |
| Shared academic HPC center, multiple users/groups, on-premise or capacity-limited QC | 3 (Slurm + SPANK), with Slurm licenses enabled |
| Dedicated production cluster, single tenant, still scheduler-managed | 3 (Slurm + SPANK), partition gating optional, licenses optional |
| Site already standardized on Spack for software management | 4, layered on top of 1-3 |
| Plugin development or CI, no access to real Slurm | 5's Docker topology only — not a deployment target |
| Site running PBS, LSF, or Kubernetes instead of Slurm | 7 — no supported path today; would require new integration work |
