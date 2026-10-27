param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('offline-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='call-budget-session-fixture-20261027';$env:HIBERNATION_BUDGET_KEY='budget-provider-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(15);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer call-budget-session-fixture-20261027'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))};Invoke-RestMethod @p}
function Start-Backend{$script:service=Start-Process $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log);Wait-For {if($script:service.HasExited){throw 'backend exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$script:service.Id}} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};$service.WaitForExit(10000)|Out-Null;Check $service.HasExited 'graceful shutdown'}
function Idle{Wait-For {!(Api '/api/chat').busy} 'turn complete'}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Idle}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@()}}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 40|ConvertFrom-Json -AsHashtable}
function Save($Value){$null=Api '/api/settings' @{settings=$Value}}
function Funds($Available,$Amount='0.00',$File='balance.json'){@{is_available=$Available;balance_infos=@(@{currency='CNY';total_balance=$Amount})}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $root ('provider/'+$File)) -Encoding utf8}
function Balance{(Api '/api/dashboard').balance_control}
function Fresh-Check{Wait-For {([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-(Balance).last_attempt_ms)-ge5100} 'refresh cooldown';$null=Api '/api/balance/check' @{};Wait-For {!(Balance).checking} 'balance check finished'}
try{
 New-Item -ItemType Directory -Force (Join-Path $root provider)|Out-Null
 $env:HA_BALANCE_ACTOR='balance-actor-fixture';$env:HA_BALANCE_REVIEWER='balance-reviewer-fixture'
 Funds $false
 $provider=Start-Process $ProviderExecutable -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Wait-For {Test-Path (Join-Path $root provider/port)} 'provider';$port=[int](Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.plugins=@{};$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Balance fixture';protocol='chat-completions';enabled=$true;origin="http://127.0.0.1:$port";basePath='/v1';model='balance-fixture';credentialEnvironment='HA_BALANCE_ACTOR';timeoutMs=10000;maxOutputTokens=2048},@{id='reviewer';label='Balance review fixture';protocol='chat-completions';enabled=$true;origin="http://127.0.0.1:$port";basePath='/v1';model='balance-fixture';credentialEnvironment='HA_BALANCE_REVIEWER';timeoutMs=10000;maxOutputTokens=2048})
 $cfg|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 Wait-For {(Balance).balance_offline} 'empty account automatically offline'
 Check ((Api '/api/chat/status').offline-and!(Balance).manual_offline) 'zero balance goes offline without a user message'
 Check ((Api '/api/dashboard').model_budget.used-eq0-and(Invoke-RestMethod "http://127.0.0.1:$port/fixture/health").calls-eq0) 'balance checks spend no generation calls'
 Stop-Backend;Start-Backend;Check ((Api '/api/chat/status').offline) 'automatic offline persists across restart';Wait-For {!(Balance).checking} 'restart query'
 Funds $true '0.00';Fresh-Check;Check ((Balance).balance_offline) 'inconsistent zero balance cannot restore online'
 Set-Content (Join-Path $root provider/balance.json) '{}' -Encoding utf8;Fresh-Check;Check ((Balance).balance_offline) 'malformed response cannot restore online'
 Set-Content (Join-Path $root provider/balance-status.txt) '503';Fresh-Check;Check ((Balance).balance_offline) 'network/provider failure retains automatic offline'
 Remove-Item -LiteralPath (Join-Path $root provider/balance-status.txt);$null=Api '/api/control' @{offline=$true};Funds $true '1.00';Fresh-Check
 Check (!(Balance).balance_offline-and(Balance).manual_offline-and(Api '/api/chat/status').offline) 'funded account clears auto reason but never overrides manual offline'
 $null=Api '/api/control' @{offline=$false};Check (!(Api '/api/chat/status').offline) 'operator may return online after balance recovery'
 Funds $false '0.00' 'reviewer-balance.json';$settings=Settings;$settings.review.enabled=$true;$settings.review.profileId='reviewer';Save $settings
 Wait-For {(Balance).balance_offline} 'reviewer account gate';Check ((Balance).blocked.PSObject.Properties.Name-contains'reviewer'-and(Balance).blocked.PSObject.Properties.Name-notcontains'actor') 'independent reviewer balance also gates shared runtime'
 Funds $true '2.00' 'reviewer-balance.json';Fresh-Check;Wait-For {!(Api '/api/chat/status').offline} 'all required accounts restored';Check (!(Api '/api/chat/status').offline) 'both actor and reviewer funded resumes automatically'
 $settings=Settings;$settings.review.enabled=$false;Save $settings
 @{nonce='402-auto';responses=@(@{http_status=402;response=@{}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id='provider-402';text='simulate real provider balance failure'}
 Wait-For {(Balance).balance_offline} 'HTTP 402 latch';Check ((Api '/api/chat/status').offline-and!(Api '/api/chat/status').call_failed) '402 goes offline silently even after a previous positive check'
 $before=(Invoke-RestMethod "http://127.0.0.1:$port/fixture/balance-health").checks;Start-Sleep -Milliseconds 500
 Check ((Invoke-RestMethod "http://127.0.0.1:$port/fixture/balance-health").checks-eq$before) '402 never causes rapid balance polling or paid recovery probes'
 Plan @((Reply '余额恢复后继续。'));Fresh-Check;Wait-For {!(Api '/api/chat/status').offline} '402 recovers from official availability'
 Check (!(Api '/api/chat/status').offline) 'positive balance recovers from provider failure'
 Stop-Backend
 $raw=[string](Get-Content (Join-Path $root service.log) -Raw);Check (!$raw.Contains($env:HA_BALANCE_ACTOR)-and!$raw.Contains($env:HA_BALANCE_REVIEWER)) 'credentials absent from logs'
 Write-Output "Automatic balance checks passed: $checks checks. Evidence: $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
}
