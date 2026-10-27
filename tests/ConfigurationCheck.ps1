param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$checks=0
$root=Join-Path $ScratchRoot ('configuration-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
$cfg.dataRoot=Join-Path $root 'must-not-exist';$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $root missing-plugins
$cfg.plugins=@{napcat=@{enabled=$false}};$file=Join-Path $root config.json
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Run{$cfg|ConvertTo-Json -Depth 45|Set-Content -LiteralPath $file -Encoding utf8;$p=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$file+'" --check-config') -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $root out.log) -RedirectStandardError (Join-Path $root error.log);$p.ExitCode}
Check ((Run)-eq0) 'configuration check accepts policy without loading missing plugin binaries'
Check (!(Test-Path -LiteralPath $cfg.dataRoot)) 'preflight does not create workspace archives keys or service locks'
$cfg.profiles[0].reasoningEffort='unsupported-value';Check ((Run)-ne0) 'invalid effort is rejected before initialization'
Check (!(Test-Path -LiteralPath $cfg.dataRoot)) 'failed validation leaves no runtime'
Check ((Get-Content -LiteralPath (Join-Path $root error.log) -Raw).Trim()-eq'configuration_validation_failed') 'validation failure does not echo configuration values'
$cfg.profiles[0].Remove('reasoningEffort');$cfg.review.retainConversationFocus=$true;Check ((Run)-eq0) 'optional conversation focus flag is supported without starting a reviewer'
$cfg.review.retainConversationFocus='true';Check ((Run)-ne0-and!(Test-Path -LiteralPath $cfg.dataRoot)) 'conversation focus flag must be an actual boolean without initialization side effects'
$cfg.review.retainConversationFocus=$false;$cfg.reflectOnActivities=$true;Check ((Run)-eq0) 'activity reflection is an optional independent configuration flag'
$cfg.reflectOnActivities='true';Check ((Run)-ne0) 'activity reflection rejects a string instead of a boolean'
@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
