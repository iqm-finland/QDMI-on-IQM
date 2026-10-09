# Choose an integration

Use the same IQM QDMI device implementation from a workstation or cluster.
Select the client and installation method that suit the workload.

| Use case                                                 | Guide                                                                            |
| -------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Call the QDMI C interface from a native application      | [Usage](usage.md)                                                                |
| Compile and execute Qiskit circuits                      | [Qiskit](qiskit.md)                                                              |
| Schedule workloads on an HPC cluster                     | [IQM on Slurm](spank_plugin.md)                                                  |
| Submit Python sampling and estimation jobs through Slurm | [Offloader](python_package.md#programmatic-offloading-with-the-offloader-module) |
| Install with a site software manager                     | [Spack](spack_guide.md)                                                          |
| Exercise the integration without IQM access              | [Local smoke test](spank_plugin.md#test-locally)                                 |

Slurm runs above MQT Core's QDMI client interface. It controls cluster
admission; IQM authentication and device queues remain separate. Workstation
applications can use the same QDMI and Qiskit interfaces without a scheduler.
