param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CtestArgs
)

Set-StrictMode -Version Latest

& ctest @CtestArgs
if ($LASTEXITCODE -eq 0) {
    exit 0
}

$failed = $LASTEXITCODE
for ($attempt = 1; $attempt -le 3; $attempt++) {
    Start-Sleep -Seconds 2
    & ctest @CtestArgs --rerun-failed
    if ($LASTEXITCODE -eq 0) {
        exit 0
    }
}

exit $failed
