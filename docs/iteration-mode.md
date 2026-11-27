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

Choose [Python](#use-a-reservation-from-python) for Qiskit or MQT Core, or
[the C device API](#use-the-c-device-api) for native code. For a batch job, see
[Run an IQM iteration on Slurm](slurm.md#run-an-iqm-iteration-on-slurm).

## Use a reservation from Python

Install `iqm-qdmi[qiskit]` as shown in the [Qiskit guide](qiskit.md). Compile
your first circuit before requesting time. Inside the `with` block, use
`reserved` to run circuits on the reservation.

```python
from iqm.qdmi.qiskit import IQMBackend
from qiskit import QuantumCircuit

backend = IQMBackend(qc_alias="emerald:mock")
circuit = QuantumCircuit(1)
circuit.h(0)
circuit.measure_all()
compiled = backend.compile(circuit)

with backend.reservation(max_duration_seconds=60, idle_timeout_seconds=30) as reserved:
    print(reserved.options.reservation_id)
    counts = reserved.run(compiled, shots=128).result().get_counts()
    print(counts)
```

Use `counts` to choose the next circuit inside the block. On exit, the context
requests graceful end and waits up to 300 seconds for confirmation, including
when your code raises an exception. The original `backend` runs on demand.

To join an existing reservation, pass its UUID to
`backend.run(compiled, reservation_id=reservation_id)`. If you use
[MQT Core directly](python_package.md#querying-the-device-directly), import
`reservation` from `iqm.qdmi.iteration`. The context
`with reservation(device, max_duration_seconds=60) as reservation_id:` yields
the UUID to pass as `custom2=reservation_id` on each circuit submission.

If cleanup fails, keep the UUID from the error for [recovery](#handle-failures).
If the workload also raised an exception, its cleanup details appear in a note
on that exception.

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
`{"end":"<uuid>"}`. Running work can finish. Use `job_wait` with a finite
timeout on `owner` to confirm termination, then free both handles with
`IQM_QDMI_device_job_free`. From a fresh session, wait on the end job instead.
Freeing the owner of an open reservation requests graceful end on a best-effort
basis, with a 30-second HTTP budget.

## Handle failures

In Python, reopen the same target and call
`backend.device.retrieve_job_by_id(reservation_id).check()`. If the reservation
is still open, use
`backend.device.submit_job(json.dumps({"end": reservation_id}), ProgramFormat.CUSTOM2)`
and call `wait(300)` on the returned job. Import `json` and `ProgramFormat` from
the standard library and `mqt.core.qdmi`, respectively. Keep the UUID if the
wait times out.

In C, open a fresh session on the same target and retrieve the reservation with
`IQM_QDMI_device_session_retrieve_device_job_by_id`. If it is still open, use
the saved ID to [end it](#end-the-reservation). Freeing a retrieved handle only
releases that handle. Keep the ID until IQM reports `DONE`, `CANCELED`, or
`FAILED`.

If creation reports an error, check whether the owner still exposes a UUID and
save it for cleanup. If the request may have reached IQM and you have no UUID,
ask the service administrator to reconcile it using the target and request time
before creating another reservation.

Submitting a circuit to an ended reservation returns
`QDMI_ERROR_INVALIDARGUMENT` (HTTP 409). Authentication and timeout errors
return `QDMI_ERROR_PERMISSIONDENIED` and `QDMI_ERROR_TIMEOUT`, respectively.
