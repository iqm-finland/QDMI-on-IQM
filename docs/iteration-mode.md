# Reserve a quantum computer with QDMI jobs

An IQM reservation groups circuit jobs on one quantum computer. Open a QDMI
device session, submit a reservation job, and read its ID. Each circuit you
attach to that ID keeps its own job handle, status, and results.

## Before you start

Select an iteration-enabled target with `IQM_QUANTUM_COMPUTER` or `IQM_QC_ID`
and configure `IQM_SERVER_URL`. Provide a workspace token with `job_execution`
scope through `IQM_TOKEN` or `IQM_TOKENS_FILE`. Initialize the device session as
usual. The reservation uses that session's credentials and target. The device
advertises the reservation format. The server determines whether the selected
quantum computer accepts it.

## Request a reservation

Use `QDMI_PROGRAM_FORMAT_CUSTOM2` with one NUL-terminated JSON program. Specify
`max_duration_seconds` from 1 through 2700. Omit `idle_timeout_seconds` or use
zero for the server default. A positive idle timeout must fit within the maximum
duration. Both limits start at activation.

```c
#include <iqm_qdmi/device.h>
#include <string.h>

/* session is an initialized IQM_QDMI_Device_Session. */
IQM_QDMI_Device_Job owner = NULL;
const char request[] = "{\"max_duration_seconds\":60,\"idle_timeout_seconds\":15}";
const void *programs[] = {request};
const size_t sizes[] = {sizeof(request)};
int status = IQM_QDMI_device_session_create_device_job(session, &owner);
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_set_programs(
        owner, QDMI_PROGRAM_FORMAT_CUSTOM2, 1, sizes, programs);
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_submit(owner);
```

Read `QDMI_DEVICE_JOB_PROPERTY_ID` into a 37-byte character buffer. Persist the
UUID if the application needs to recover after a restart. Keep the owner handle
alive throughout the workload.

## Submit circuits

Set `QDMI_DEVICE_JOB_PARAMETER_CUSTOM2` on each participating circuit job,
before submission. The value is the lowercase reservation UUID, including the
trailing NUL. This works with IQM JSON and QIR circuits.

```c
char id[37];
status = IQM_QDMI_device_job_query_property(
    owner, QDMI_DEVICE_JOB_PROPERTY_ID, sizeof(id), id, NULL);
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_set_parameter(
        circuit, QDMI_DEVICE_JOB_PARAMETER_CUSTOM2, strlen(id) + 1, id);
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_submit(circuit);
```

The device forwards the submission to IQM immediately. An ordinary circuit
without the ID uses on-demand execution. The same session can submit both kinds.
Another session with access to the same target can also use the UUID.

Use `job_check` on the reservation to distinguish `QUEUED` (waiting for IQM)
from `RUNNING` (active). Use `job_wait` on a **circuit** to wait for its
results. Waiting on the **reservation** waits for its terminal state.

For a shared-cluster example, see
[Run an IQM iteration on Slurm](slurm.md#run-an-iqm-iteration-on-slurm).

## End the reservation

For checked cleanup, submit a second `CUSTOM2` job with one JSON program of the
form `{"end":"<uuid>"}`. IQM allows running work to finish. Use a finite
`job_wait` on the returned handle to confirm termination. An HTTP 202
acknowledgement can arrive while the reservation is still active.

Freeing the creating handle makes a bounded best-effort request to end an
unfinished reservation, with a maximum HTTP budget of 30 seconds. For recovery,
use the saved UUID with `IQM_QDMI_device_session_retrieve_device_job_by_id` in a
fresh session. Freeing a retrieved handle releases it locally. Use the explicit
end program to end the reservation from that session. Circuit cancellation
remains a separate operation.

## Handle failures

After a failed submission, check whether the owner exposes a confirmed UUID.
Keep that ID for cleanup. If the server may have accepted creation but no ID was
returned, ask the service administrator to reconcile the request using the
target and request time before creating another reservation. A queued
reservation can outlive its submitting process.

Submitting a circuit to an ended reservation returns
`QDMI_ERROR_INVALIDARGUMENT` (HTTP 409). Authentication and timeout failures
return `QDMI_ERROR_PERMISSIONDENIED` and `QDMI_ERROR_TIMEOUT`, respectively.
Result downloads use the session's normal request timeout.

On an end or wait error, retain the UUID, inspect the reservation, and retry
cleanup within a deadline. Redacted device diagnostics include the server's
request ID when available. Clear a persisted UUID only after observing `DONE`,
`CANCELED`, or `FAILED`.
