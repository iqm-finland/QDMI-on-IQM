# Administer IQM workloads on Slurm

Follow
[MQT Core's Slurm guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
for scheduler configuration, license counts, and job environments.

Install the IQM device implementation in the workload environment on the compute
nodes. Then follow [IQM on Slurm](spank_plugin.md) to select a catalogue entry,
provide credentials, and submit a Qiskit job. Catalogue and credential paths
must be readable by the job user on every participating node.

[Spack](spack_guide.md) is also available for sites that manage software through
Spack environments.
