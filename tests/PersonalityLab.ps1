param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$node=if($env:HA_NODE_RUNTIME){$env:HA_NODE_RUNTIME}else{'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'}
& $node (Join-Path $ProjectRoot tests/PersonalityBudget.mjs) (Join-Path $BuildRoot integration)
if($LASTEXITCODE-ne0){throw 'Experiment budget verification failed'}
$root=Join-Path $ProjectRoot ('artifacts/personality-runner-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$episode=Join-Path $root episode.json;$lab=Join-Path $root continuing
$personaHash=(Get-FileHash (Join-Path $ProjectRoot character/persona.json)).Hash
@{version=1;title='synthetic runner continuity';turns=@(@{text='合成记录：橱窗角落留着一只纸船。'})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $episode -Encoding utf8
& (Join-Path $ProjectRoot tools/PersonalityLab.ps1) -ProjectRoot $ProjectRoot -BuildRoot $BuildRoot -LabRoot $lab -Episode $episode -NodeExecutable $node
$keyHash=(Get-FileHash (Join-Path $lab data/runtime/memory-key.dpapi)).Hash
$first=@(Get-ChildItem (Join-Path $lab episodes) -Directory)[0];$a=Get-Content (Join-Path $first.FullName report.json) -Raw|ConvertFrom-Json -Depth 90
$initial=Get-Content (Join-Path $first.FullName config.json) -Raw|ConvertFrom-Json
if($initial.plugins.reading.enabled-or$initial.plugins.reading.autonomousReading){throw 'External reading should remain off unless explicitly configured'}
@{version=1;title='synthetic runner second encounter';referenceReading=$true;turns=@(@{text='合成记录：我们先把那件事放一会儿。'})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $episode -Encoding utf8
& (Join-Path $ProjectRoot tools/PersonalityLab.ps1) -ProjectRoot $ProjectRoot -BuildRoot $BuildRoot -LabRoot $lab -Episode $episode -NodeExecutable $node
$second=Get-ChildItem (Join-Path $lab episodes) -Directory|Where-Object Name -ne $first.Name;$b=Get-Content (Join-Path $second.FullName report.json) -Raw|ConvertFrom-Json -Depth 90
$continued=Get-Content (Join-Path $second.FullName config.json) -Raw|ConvertFrom-Json
if(!$continued.plugins.reading.enabled-or$continued.plugins.reading.autonomousReading-or$continued.plugins.reading.maxRequestsPerHour-ne12){throw 'Explicit manual reading should not silently grant autonomous networking or remove its limit'}
if($keyHash-ne(Get-FileHash (Join-Path $lab data/runtime/memory-key.dpapi)).Hash){throw 'Continuing archive key changed'}
if($b.turns[0].trace.selected_ids-notcontains$a.turns[0].trace.source_event_id){throw 'Prior encounter was not available after restart'}
if($personaHash-ne(Get-FileHash (Join-Path $ProjectRoot character/persona.json)).Hash){throw 'Public persona was modified by the lab'}
if((Get-ChildItem (Join-Path $lab data/runtime/secrets) -File).Count-or(Test-Path (Join-Path $lab runner.lock))){throw 'Runner leaked credentials or lock'}
if($a.production_archive_read-or$b.production_archive_read-or!$a.continuing_archive-or!$b.continuing_archive){throw 'Lab provenance flags invalid'}
Write-Output 'Personality runner: persistent synthetic archive, isolated persona, frozen host, no production records and clean lifecycle passed.'
