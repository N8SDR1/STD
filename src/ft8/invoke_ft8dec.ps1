param(
  [Parameter(Mandatory = $true)][string]$Script,
  [Parameter(Mandatory = $true)][string]$Out
)
& $Script $Out
if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
