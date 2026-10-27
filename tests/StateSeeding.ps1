param([Parameter(Mandatory)][string]$FixtureExecutable,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$root=Join-Path $ScratchRoot ('state-seed-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
$null=& $FixtureExecutable $root engaged;Check ($LASTEXITCODE-ne0-and!(Test-Path -LiteralPath (Join-Path $root data))) 'fixture refuses an unmarked directory without creating files'
[IO.File]::WriteAllText((Join-Path $root .synthetic-ai-evaluation),'hibernation.dialogue-evaluation.v1',[Text.UTF8Encoding]::new($false))
$null=& $FixtureExecutable $root 'arbitrary';Check ($LASTEXITCODE-ne0-and!(Test-Path -LiteralPath (Join-Path $root data))) 'unknown state mode has no side effect'
$output=& $FixtureExecutable $root engaged;Check ($LASTEXITCODE-eq0) 'synthetic state can initialize one fresh encrypted archive'
$value=$output|ConvertFrom-Json;Check ($value.synthetic-and$value.experience_records_created-eq0-and$value.continuous_state.energy-eq.84) 'seed is explicitly synthetic and creates no invented experiences'
$database=Join-Path $root data/archive/records.sqlite;$before=(Get-FileHash -LiteralPath $database -Algorithm SHA256).Hash
$null=& $FixtureExecutable $root withdrawn;Check ($LASTEXITCODE-ne0-and(Get-FileHash -LiteralPath $database -Algorithm SHA256).Hash-eq$before) 'fixture cannot overwrite even its own existing archive'
@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
