# Administrator Guide: IQM on a Slurm Cluster

[MQT Core's canonical Slurm guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
owns scheduler configuration, static license admission, shared SPANK deployment,
and operations. Use that guide for the common cluster setup.

For this provider:

1. Choose the wheel-provided or native/system runtime in
   [IQM on Slurm](spank_plugin.md#choose-the-provider-runtime).
2. Define a
   [concrete site catalogue](spank_plugin.md#define-a-concrete-site-catalogue)
   with the installed library, endpoint, and exact IQM QC ID.
3. Provision job-user access to the provider's
   [credential file](spank_plugin.md#credentials).
4. Configure the same concrete catalogue IDs as Core's static Slurm licenses.
5. Validate both ordinary job-environment configuration and optional shared
   injection using the
   [provider fixture](spank_plugin.md#validate-the-migration).
6. Follow the
   [migration instructions](spank_plugin.md#migrate-the-provider-plugin) before
   removing an existing IQM plugin deployment.

[Spack installation](spack_guide.md) remains an alternative way to install the
provider library. It does not provide a separate scheduler implementation.
