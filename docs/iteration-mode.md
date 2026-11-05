# Standalone iteration management

The IQM extension in `iqm_qdmi/iteration.h` creates, inspects, and ends
iterations without a QDMI device session. It is installed with native packages
and Python wheels. An iteration reserves one quantum computer. Your application
owns its ID and must explicitly end it; freeing a client or device session does
not end it.

## Native interface

Allocate one client and reuse it for serial operations. Separate clients can run
concurrently. Allocation copies its inputs without network access. Null
connection inputs use `IQM_SERVER_URL`, then `IQM_BASE_URL`. Null authentication
inputs use `IQM_TOKEN` or `IQM_TOKENS_FILE`; select only one source. Explicit
authentication overrides these defaults. Use a personal workspace token with
`job_execution` scope and access to the selected quantum computer. Read access
alone does not prove iteration creation permission. Keep tokens out of logs and
command arguments.

```c
#include <iqm_qdmi/iteration.h>
#include <stdio.h>

IQM_QDMI_Iteration_Client client = NULL;
IQM_QDMI_Iteration_Result result = {0};
int status = IQM_QDMI_iteration_client_alloc(NULL, NULL, NULL, 30000, &client);
if (status == QDMI_SUCCESS) {
    status = IQM_QDMI_iteration_create(client, "emerald:mock", 60, 60,
                                      30000, &result);
    if (result.iteration_id[0] != '\0') {
        /* Record the ID, including partial create failures. */
        printf("Iteration to reconcile: %s\n", result.iteration_id);
        status = IQM_QDMI_iteration_end(client, result.iteration_id,
                                       30000, &result);
        /* On failure or HTTP 202, inspect this ID until terminal. */
    }
}
IQM_QDMI_iteration_client_free(client);
```

All operations require a nonnull result. Create accepts a nonempty quantum
computer UUID or alias, a maximum duration of 1–2700 seconds, and an idle
timeout of 1–maximum seconds. Idle zero uses the server's default (currently 60
seconds). Maximum duration starts on activation, not creation; neither timeout
bounds time in the queue. Idle time includes classical processing while no job
runs or waits.

`get` and `end` accept canonical lowercase UUIDs and allow
`get(client, result.iteration_id, timeout_ms, &result)`. They retain that input
ID on failure and reject a mismatching server ID. Server UUIDs are normalized to
lowercase. Known states are `waiting`, `active`, `completed`, `cancelled`, and
`failed`. The last three are terminal. Unknown states are protocol errors; the
result state stays empty and diagnostics retain the value unless redacted. An
active state alone does not prove future job acceptance: an iteration may
already be ending. The server activates waiting iterations automatically.

End returns its POST response without polling. HTTP 200 means ended; 202 means
in-flight work is still finishing. After 202, inspect immediately, then sleep 1,
2, 4, 5, 5… seconds between reads, bounded by your cleanup deadline. Retry an
uncertain end within that same budget: ending is idempotent. Do not keep ending
after a confirmed 202. In-flight work can outlast cleanup; the server may allow
it up to five minutes after the iteration ends.

## Errors and deadlines

Each positive `timeout_ms` sets a monotonic deadline for HTTP requests,
including quota waits and 429 retries. Each HTTP attempt is capped by both the
client request timeout and the remaining operation budget. Defaults in the
example are 30 seconds; use smaller remaining budgets when cleaning up. A
sub-millisecond remainder expires instead of becoming an unlimited request.
Synchronous token-file reads and response decoding cannot be interrupted by this
deadline; the call can therefore take longer. Use an explicit token to avoid
file I/O during an operation. Local transport/deadline expiry returns
`QDMI_ERROR_TIMEOUT`. Other errors use the existing QDMI HTTP mapping: 401/403
permission denied, 404 not found, 400/429 invalid argument, and server/protocol
failures fatal. Inspect `error_code` to distinguish authentication, access,
unavailable iteration mode, and missing iterations. A quota block lasting the
entire budget can prevent confirmed cleanup.

`IQM_QDMI_iteration_client_diagnostics` borrows a NUL-terminated JSON string
until the next operation or free. Copy it promptly. It contains HTTP status and
selected server `error_code`, `message`, and `request_id`, without arbitrary
headers or response fields. The result preserves HTTP status and any confirmed
ID on partial failure. Fields containing the bearer token become `[redacted]`.
Recoverable allocation failure returns `QDMI_ERROR_OUTOFMEM` without discarding
an already confirmed ID. No HTTP response means status zero. The pinned JSON
dependency can terminate under severe memory exhaustion during destruction; that
process termination cannot become a QDMI error (see
[upstream issue 5135](https://github.com/nlohmann/json/issues/5135)).

Management requests verify TLS and do not follow redirects. Explicit 429
refusals may be retried within budget. Ambiguous create transport, 5xx,
redirect, or decoding outcomes are never automatically retried. An unreadable or
missing create ID is reported with `creation_uncertain: true`. If an ID is
present even on failure, retain it and attempt bounded cleanup.

For an uncertain create without an ID, inspect your own server list using
`GET /api/v1/iterations?qc=emerald%3Amock&status=waiting&status=active&page=1&page_size=100`.
Follow pagination, and inspect creation times and other returned fields against
your operation's records. Include terminal states by omitting the status filter
when reconciling older requests. More than one candidate may match; the API
provides no creation label or idempotency key. Never guess an ID or end an
unrelated iteration. Reconcile ambiguous cases with the service administrator.

## Designated live validation

Configure with `-DBUILD_IQM_QDMI_TESTS=ON`, then build the explicit
`iteration_management_smoke` target. It is excluded from default builds and is
never registered in CTest or wheel pytest collection. The reusable
iteration-validation workflow runs it once on trusted native changes, after
other token-using CI jobs. It uses `emerald:mock`, a 60-second maximum/idle
duration, a queue allowance of 120 seconds, and a separate 30-second cleanup
budget. Permission and target failures fail the job; they are not skipped or
retried until passing.

Set `IQM_ITERATION_SCRATCH_FILE` to a runner-local path. Immediately after
confirmed creation, including a partial create failure, the executable appends
and flushes one canonical UUID per line. It removes an ID only after confirmed
terminal state. Its `--cleanup-only` mode validates saved IDs and reconciles
only those IDs within 30 seconds total, with each get/end capped at ten seconds
and the remaining budget. The workflow runs this mode in an `always()` step.
Repeated cleanup is safe. This file contains IDs, never credentials.

Superseded CI runs cancel normally; only the lifecycle job preserves its live
and cleanup steps. This lets an in-flight create save its ID and reach cleanup.
Setup and build steps remain cancellable. A cancellation check prevents a new
create when cancellation is already known.

Lifecycle jobs serialize by quantum computer across runs; other jobs may
overlap. GitHub's lifecycle queue holds at most 100 pending jobs. Overflow or a
missing expected live job fails required coverage; inspect and rerun the
current-head run.

:::{important}
Runner-local scratch does not survive runner loss. Server timeouts only limit
occupancy after activation, so lost queued IDs require manual or server-side
reconciliation. Cleanup is not guaranteed after force cancellation, process or
node loss, an uncertain create without an ID, or an exhausted deadline.
:::
