# Manage an IQM iteration

Use an iteration to reserve a quantum computer for a limited time. From C, C++,
or Python, you can create a reservation, check its status, and end it without
opening a QDMI device session.

The management functions are available in `iqm_qdmi/iteration.h`, included with
the native library and Python wheels. Use the QDMI device interface to submit
circuits within a reservation. See the [C/C++ API reference](capi/index.md) for
the function signatures.

## Before you start

Set `IQM_SERVER_URL` to your IQM server, for example
`https://resonance.iqm.tech`. Provide your personal workspace token through
`IQM_TOKEN`, or set `IQM_TOKENS_FILE` to your token file. Use only one token
source and keep tokens out of logs and command arguments.

Your token needs `job_execution` scope and access to the quantum computer. Use
its alias, such as `emerald:mock`, or its UUID when creating an iteration. Your
server must support iteration mode.

## Use an iteration from Python

The `iqm-qdmi` package includes an iteration management client. Install
`iqm-qdmi[qiskit]` to submit Qiskit circuits within the reservation.

```python
from iqm.qdmi.iteration import IterationClient, IterationError
from iqm.qdmi.qiskit import IQMBackend
from qiskit import QuantumCircuit

with IterationClient() as client:
    try:
        iteration = client.create("emerald:mock", 60, idle_timeout_seconds=60)
    except IterationError as error:
        # Save this ID for recovery if the server confirmed creation.
        print(error.result.iteration_id)
        raise

    iteration_id = iteration.iteration_id
    assert iteration_id is not None
    print(iteration_id)
    try:
        backend = IQMBackend(qc_alias="emerald:mock", iteration_id=iteration_id)
        circuit = QuantumCircuit(1)
        circuit.h(0)
        circuit.measure_all()
        print(backend.run(backend.compile(circuit), shots=128).result().get_counts())
    finally:
        ended = client.end(iteration_id)
        print(ended.state)
```

Set the same server URL, credentials, and quantum computer for management and
execution. You can attach another backend to the same ID. Its sampler and
estimator share the reservation through that backend's device session.

Closing the client or backend leaves the reservation on the server. Call
`client.end(iteration_id)` explicitly. If the state is still `waiting` or
`active`, use `client.get(iteration_id)` with a delay and an overall deadline
until it is `completed`, `cancelled`, or `failed`. You can use a new client to
finish cleanup later.

When creation fails, use any ID in `error.result` to inspect or end the
reservation. If the ID is absent and
`error.diagnostics.get("creation_uncertain")` is true, check the server's
iteration listing before making another creation request.

## Create an iteration

Allocate a client, then request a reservation. This example allows up to 60
seconds of use and ends the reservation after 60 idle seconds. Each request has
a 30-second timeout.

```c
#include <iqm_qdmi/iteration.h>
#include <stdio.h>
#include <string.h>

IQM_QDMI_Iteration_Client client = NULL;
IQM_QDMI_Iteration_Result result = {0};
int status = IQM_QDMI_iteration_client_alloc(NULL, NULL, NULL, 30000, &client);
if (status != QDMI_SUCCESS) {
    return 1;
}

status = IQM_QDMI_iteration_create(client, "emerald:mock", 60, 60,
                                  30000, &result);
if (result.iteration_id[0] != '\0') {
    /* Save this ID so you can inspect or end the reservation later. */
    printf("Iteration ID: %s\n", result.iteration_id);
}
if (status != QDMI_SUCCESS) {
    fprintf(stderr, "%s\n", IQM_QDMI_iteration_client_diagnostics(client));
}
```

Keep the returned ID even if creation reports an error: the reservation may
already exist. Save it somewhere your application can recover after a restart.
Continue with the examples below only when `result.iteration_id` is nonempty.
Otherwise, follow the recovery steps under
[Handle failures](iteration-mode.md#handle-failures).

Choose a maximum duration between 1 and 2,700 seconds. Choose an idle timeout
between 1 second and that maximum, or use `0` for the server default. Idle time
includes your application's calculations while no job is running or queued. Both
limits apply after activation; they do not limit time spent waiting for the
quantum computer.

## Check whether it is ready

Pass the saved ID to `IQM_QDMI_iteration_get`. You can reuse `result` as shown:

```c
status = IQM_QDMI_iteration_get(client, result.iteration_id, 30000, &result);
if (status == QDMI_SUCCESS) {
    printf("Iteration status: %s\n", result.state);
}
```

| Status                             | What to do                                                                   |
| ---------------------------------- | ---------------------------------------------------------------------------- |
| `waiting`                          | Wait, then check again. The server activates the reservation automatically.  |
| `active`                           | The reservation is active. It may still stop accepting jobs if it is ending. |
| `completed`, `cancelled`, `failed` | The reservation has ended.                                                   |

Leave a delay between checks and set an overall waiting limit in your
application. If you stop waiting, end the reservation so it does not activate
later when you no longer need it.

## Run circuits within the reservation

Allocate a device session for the same quantum computer and set
`QDMI_DEVICE_SESSION_PARAMETER_CUSTOM5` before initializing it. Pass the
lowercase iteration UUID as 36 characters followed by a NUL.

```c
#include <iqm_qdmi/device.h>
#include <string.h>

status = IQM_QDMI_device_initialize();
if (status != QDMI_SUCCESS) {
    return 1;
}

/* Set IQM_QUANTUM_COMPUTER to the alias used to create the iteration. */
IQM_QDMI_Device_Session session = NULL;
status = IQM_QDMI_device_session_alloc(&session);
if (status == QDMI_SUCCESS) {
    status = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM5,
        strlen(result.iteration_id) + 1, result.iteration_id);
}
if (status == QDMI_SUCCESS) {
    status = IQM_QDMI_device_session_init(session);
}
/* On success, submit and retrieve circuit results through the QDMI job API. */
IQM_QDMI_device_session_free(session);
IQM_QDMI_device_finalize();
```

You can submit IQM JSON or QIR circuits while the iteration is waiting or
active. To use the same reservation in another session, set the same ID on that
session. If the server refuses an iteration submission, check the device log for
the server's error and request ID.

For a shared-cluster example, see
[Run an IQM iteration on Slurm](slurm.md#run-an-iqm-iteration-on-slurm).

If submission fails before you receive a job ID, check the server before
resubmitting. A transfer timeout can occur after the server accepts the job.

An iteration session defaults to a 30-second HTTP timeout. To override it, set
session `CUSTOM3` before initialization with a positive `uint64_t` millisecond
value or a NUL-terminated string such as `timeout_ms:5000`. The native job wait
polls at intervals of up to five seconds. A finite wait limits its polling and
HTTP budget. If the wait times out, check the job or wait again.

## End the reservation

Call `IQM_QDMI_iteration_end` when you finish, including after an error or
cancellation if you have an iteration ID:

```c
status = IQM_QDMI_iteration_end(client, result.iteration_id, 30000, &result);
if (status != QDMI_SUCCESS) {
    fprintf(stderr, "%s\n", IQM_QDMI_iteration_client_diagnostics(client));
} else if (strcmp(result.state, "active") == 0 || strcmp(result.state, "waiting") == 0) {
    /* Check this ID with iteration_get until its status shows it has ended. */
    printf("Waiting for running work to finish\n");
}
```

HTTP status `202` acknowledges an asynchronous end request. Confirm termination
from the returned state, polling if necessary until it is `completed`,
`cancelled`, or `failed`. Start with a one-second delay and increase it to at
most five seconds between checks. Set a cleanup deadline; running work can take
up to five minutes to finish.

If ending fails, keep the ID and retry within your cleanup deadline. Ending an
iteration more than once is safe. Once an end request succeeds with `202`, keep
checking its status instead of sending more end requests.

Release the client when you no longer need it:

```c
IQM_QDMI_iteration_client_free(client);
```

:::{important}
Freeing a client or device session does not end an iteration. Keep any ID whose
cleanup is unfinished, and use it with a new client to check or end the
reservation later. Server timeouts do not cancel a queued reservation.
:::

## Handle failures

Check each function's return value against `QDMI_SUCCESS`. Call
`IQM_QDMI_iteration_client_diagnostics(client)` for details, and copy its text
before the next operation or before freeing the client.

- **Permission denied:** check the token, its `job_execution` scope, and access
  to the selected quantum computer.
- **Not found:** check the iteration ID and the server you connected to.
- **Invalid argument:** check the alias, durations, and timeouts. This result
  can also mean a rate limit; inspect the diagnostic message.
- **Timeout:** the operation did not finish within its request budget. Keep any
  returned ID and check its status before deciding what to do next.
- **Server or response error:** keep any returned ID and check its status. If
  the error persists, contact your service administrator with the diagnostic
  message.
- **Creation failed without an ID:** if diagnostics contain
  `creation_uncertain: true`, do not create another reservation immediately. The
  server may already have accepted the first request. Ask your service
  administrator to identify it, using your target and request time. Never guess
  an ID or end another user's reservation.

Use positive timeouts in milliseconds. They cover network requests and
rate-limit waits, but local token-file reads and response processing can extend
the call. Use one client for sequential calls; allocate separate clients for
concurrent work.
