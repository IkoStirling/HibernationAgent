param([string]$ToolsRoot=$PSScriptRoot)
$ErrorActionPreference='Stop'
$runtime=Join-Path $ToolsRoot runtime
$manifest=Get-Content -LiteralPath (Join-Path $runtime manifest.json) -Raw | ConvertFrom-Json
foreach($entry in $manifest.files){
    $path=Join-Path $runtime $entry.path
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Runtime tool missing: $($entry.path)"}
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256){throw "Runtime tool checksum mismatch: $($entry.path)"}
}
$actual=@(Get-ChildItem -LiteralPath (Join-Path $runtime windows-x64) -Recurse -File)
if($actual.Count -ne $manifest.files.Count){throw 'Unexpected files in the runtime tool bundle.'}
Write-Output "Verified $($manifest.files.Count) pinned runtime tool files."
