# Administer IQM workloads on Slurm

[MQSC Orchestrator](https://github.com/munich-quantum-software/orchestrator)
provides a reference Slurm cluster for quantum and classical workloads. Its
[deployment guide](https://github.com/munich-quantum-software/orchestrator/blob/main/docs/deployment.md)
covers Slurm HRES resources, quantum access nodes, and the shared workload
environment. Register IQM devices by their QDMI catalogue IDs and adapt resource
counts and node placement to your site's policy.

Install the IQM device implementation in the workload environment on each
quantum access node. Make catalogue and credential paths readable by the job
user. The controller's availability checks need separate site-owned IQM
credentials; they do not supply credentials to user jobs.

Follow [IQM on Slurm](slurm.md) for device selection, authentication, and job
submission. [Spack](spack_guide.md) is also available for sites that manage
software through Spack environments.
