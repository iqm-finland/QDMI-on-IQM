# Manage an IQM iteration

Use an iteration to reserve a quantum computer for a limited time. From C or
C++, you can create a reservation, check its status, and end it without opening
a QDMI device session.

The functions below are available in `iqm_qdmi/iteration.h`, included with the
native library and Python wheels. See the [C/C++ API reference](capi/index.md)
for the full function signatures. This interface manages reservations; it does
not submit jobs or attach them to an iteration.

## Before you start

Set `IQM_SERVER_URL` to your IQM server, for example
`https://resonance.iqm.tech`. Provide your personal workspace token through
`IQM_TOKEN`, or set `IQM_TOKENS_FILE` to your token file. Use only one token
source and keep tokens out of logs and command arguments.

Your token needs `job_execution` scope and access to the quantum computer. Use
its alias, such as `emerald:mock`, or its UUID when creating an iteration. Your
server must support iteration mode.

## Create an iteration

Allocate a client, then request a reservation. This example allows up to 60
seconds of use and ends the reservation after 60 idle seconds. Each request has
a 30-second timeout.

```c
#include <iqm_qdmi/iteration.h>
#include <stdio.h>

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

## End the reservation

Call `IQM_QDMI_iteration_end` when you finish, including after an error or
cancellation if you have an iteration ID:

```c
status = IQM_QDMI_iteration_end(client, result.iteration_id, 30000, &result);
if (status != QDMI_SUCCESS) {
    fprintf(stderr, "%s\n", IQM_QDMI_iteration_client_diagnostics(client));
} else if (result.http_status == 202) {
    /* Check this ID with iteration_get until its status shows it has ended. */
    printf("Waiting for running work to finish\n");
}
```

On success, HTTP status `200` means the iteration has ended. Status `202` means
running work is still finishing: check its status until it is `completed`,
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
