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

## 3. Core Static-License Slurm Deployment

Use
[MQT Core's canonical Slurm setup](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
with the [IQM runtime, catalogue, and credentials](spank_plugin.md). Slurm
handles license admission; Core supplies optional configuration-reference
injection. The IQM provider authenticates and executes work in the job process.

Applications pass `slurm.open_device_from_license()` to
`IQMBackend(device=...)`. The existing offloader remains available for
submitting IQM CLI workers; its explicit target arguments are worker options and
do not need a provider SPANK plugin.

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

## 5. Shared Docker Integration Tests

The [provider test fixture](spank_plugin.md#validate-the-migration) supplies a
local IQM endpoint and workload to Core's common Dockerized Slurm runner. Core
owns the controller, compute services, cgroups, transport tests, and teardown.
Production deployments use the same shared module built against the cluster's
Slurm headers.

## 6. Shared or Dedicated Clusters

Choose static license counts and access controls according to the site's
concurrency and account policies. Core's operations guide describes these
scheduler choices. IQM authentication remains a separate provider control.

## 7. Forward-Looking: Non-Slurm Schedulers (Not Implemented)

Sites running other schedulers need their own job-environment integration.
Core’s shared SPANK component is specific to Slurm:

- **PBS/OpenPBS**: would need a PBS hook (`qmgr` server/queue hooks) written
  against PBS's own hook API to inject `IQM_*` variables into the job
  environment at launch — a different lifecycle and API from SPANK's.
- **LSF**: would need an `esub`/`eexec` job submission wrapper or an LSF
  external scheduler plugin; LSF has no direct SPANK equivalent, so environment
  injection and any launch-time validation would need to be reimplemented
  against LSF's job-control hooks.
- **Grid Engine**: would need a prolog/epilog script pair (or a JSV — Job
  Submission Verifier — script) configured cluster-wide to inject `IQM_*`
  variables before the job starts, since Grid Engine has no SPANK-equivalent
  plugin API; partition gating's closest analogue is queue-based access control
  via `qconf`.
- **Kubernetes (CRD-based)**: would look structurally different from all of the
  above — a Custom Resource Definition (e.g. an `IQMJob` CRD) reconciled by a
  custom controller/operator, which injects `IQM_*` variables into the pod spec
  it creates, rather than a mutating admission webhook intercepting arbitrary
  pods; Kubernetes has no per-job-step CLI flag-parsing hook comparable to
  SPANK, and partition gating's closest analogue would be namespace- or
  `ResourceQuota`-scoped access.
- **Flux**: would need a Flux plugin against its job execution API (e.g. a
  `flux-shell` plugin or jobtap plugin) to inject `IQM_*` variables at job shell
  launch — architecturally closer to SPANK's shell-launch hook than the others
  here, but a distinct plugin API and ABI that would need its own
  implementation.

None of these are implemented in this repository. Treat this section as scoping
input for a site evaluating IQM integration outside Slurm, not as supported
functionality.

## Recommendation Matrix

| Site profile                                                                         | Recommended scenario                                            |
| :----------------------------------------------------------------------------------- | :-------------------------------------------------------------- |
| Single researcher, workstation or notebook, direct network access to the IQM service | 1 (C++) or 2 (Python/Qiskit)                                    |
| Shared academic HPC center, multiple users/groups, on-premise or capacity-limited QC | 3 (Core static-license Slurm setup)                             |
| Dedicated production cluster, single tenant, still scheduler-managed                 | 3 (Core static-license Slurm setup)                             |
| Site already standardized on Spack for software management                           | 4, layered on top of 1-3                                        |
| Plugin development or CI, no access to real Slurm                                    | 5's Docker topology only — not a deployment target              |
| Site running PBS, LSF, Grid Engine, Kubernetes (CRD-based), or Flux instead of Slurm | 7 — no supported path today; would require new integration work |
