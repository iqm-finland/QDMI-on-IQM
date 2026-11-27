# Run a circuit loop with IQM iteration mode

Suppose an optimizer runs a circuit, reads the result, and uses it to choose the
next circuit. IQM's *iteration mode* lets you reserve time on one quantum
computer for that loop. You can submit each circuit as soon as it is ready.

A QDMI **session** supplies your credentials and selects the computer. A
**reservation job** requests time there and gives you a reservation ID. Each
**circuit job** you submit with that ID has its own status and results. Leave
the ID unset to run a circuit on demand.

## Before you start

Choose a target that supports iteration mode, such as `emerald:mock` on
Resonance. Follow the [session setup guide](usage.md#session-configuration) to
select the server and target. For authentication, use a workspace token with
`job_execution` scope. See
[authentication methods](usage.md#authentication-methods).

For a batch job, see
[Run an IQM iteration on Slurm](slurm.md#run-an-iqm-iteration-on-slurm).

## Use the C device API

### Request a reservation

With your session initialized, submit a reservation job. This request reserves
up to 60 seconds and releases the computer after 15 seconds of inactivity. Both
limits start at activation. Choose a maximum duration of 1 to 2700 seconds. Omit
the idle timeout or set it to zero for the server default. A positive idle
timeout must fit within the maximum duration.

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

char id[37];
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_query_property(
        owner, QDMI_DEVICE_JOB_PROPERTY_ID, sizeof(id), id, NULL);
```

Save the UUID if you may need to recover after a restart. Keep `owner` alive
while you use the reservation.

### Submit circuits

Submit the first circuit as soon as you have the ID. The device forwards the
request to IQM immediately.
[Prepare an IQM JSON or QIR circuit job](usage.md#submitting-jobs), called
`circuit` below, and attach the ID before submission.

```c
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_set_parameter(
        circuit, QDMI_DEVICE_JOB_PARAMETER_CUSTOM2, strlen(id) + 1, id);
if (status == QDMI_SUCCESS)
    status = IQM_QDMI_device_job_submit(circuit);
```

Wait on the **circuit job** and
[read its results](usage.md#retrieving-job-results) to choose the next circuit.
Attach the same ID to each new circuit. You can also use it from another session
with access to the same target.

To see whether the reservation is queued (`QUEUED`) or active (`RUNNING`), call
`job_check`. Calling `job_wait` on the **reservation job** waits for the
reservation to end.

### End the reservation

When the loop finishes, submit another `QDMI_PROGRAM_FORMAT_CUSTOM2` job with
`{"end":"<uuid>"}`. Running work can finish. Use `job_wait` on the returned
handle with a finite timeout to confirm termination, then free the handles.
Freeing the owner of an open reservation requests graceful end on a best-effort
basis, with a 30-second HTTP budget.

## Handle failures

If your process restarts, open a fresh session on the same target and retrieve
the reservation with `IQM_QDMI_device_session_retrieve_device_job_by_id`. If it
is still open, use the saved ID to [end it](#end-the-reservation). Freeing a
retrieved handle only releases that handle. Keep the ID until IQM reports
`DONE`, `CANCELED`, or `FAILED`.

If creation reports an error, check whether the owner still exposes a UUID and
save it for cleanup. If the request may have reached IQM and you have no UUID,
ask the service administrator to reconcile it using the target and request time
before creating another reservation.

Submitting a circuit to an ended reservation returns
`QDMI_ERROR_INVALIDARGUMENT` (HTTP 409). Authentication and timeout errors
return `QDMI_ERROR_PERMISSIONDENIED` and `QDMI_ERROR_TIMEOUT`, respectively.
