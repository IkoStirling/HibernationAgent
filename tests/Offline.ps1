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
try{
 New-Item -ItemType Directory -Force (Join-Path $root provider)|Out-Null
 $provider=Start-Process $ProviderExecutable -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Wait-For {Test-Path (Join-Path $root provider/port)} 'provider'
 $port=[int](Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.plugins=@{};$cfg.solitudeIntervalMs=600000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Local fixture';protocol='chat-completions';enabled=$true;origin="http://127.0.0.1:$port";basePath='/v1';model='fixture-model';timeoutMs=10000;maxOutputTokens=2048})
 $cfg|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 $null=Api '/api/control' @{offline=$true}
 $response=Invoke-WebRequest ($origin+'/api/message') -Method Post -Headers @{Authorization='Bearer call-budget-session-fixture-20261027'} -ContentType 'application/json' -Body '{"request_id":"offline-input","text":"no call"}' -SkipHttpErrorCheck
 Check ($response.StatusCode-eq409-and($response.Content|ConvertFrom-Json).error-eq'offline') 'offline rejects desktop input before storing or calling model'
 Check ((Api '/api/dashboard').model_budget.used-eq0-and(Invoke-RestMethod ("http://127.0.0.1:$port/fixture/health")).calls-eq0) 'offline uses no model budget'
 $null=Api '/api/control' @{paused=$true};$null=Api '/api/control' @{offline=$false}
 Check ((Api '/api/chat/status').paused-and!(Api '/api/chat/status').can_send) 'restoring online preserves independent pause'
 $null=Api '/api/control' @{offline=$true};$null=Api '/api/control' @{paused=$false}
 Check ((Api '/api/chat/status').offline-and!(Api '/api/chat/status').can_send) 'resuming background cannot bypass offline'
 $settings=Settings;Save $settings
 Check ((Api '/api/chat/status').offline) 'settings reload preserves offline'
 Stop-Backend;Start-Backend
 Check ((Api '/api/chat/status').offline-and!(Api '/api/chat/status').can_send) 'offline survives process restart'
 $null=Api '/api/control' @{offline=$false}
 @{nonce='balance';responses=@(@{http_status=402;response=@{}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 Message 'fixture balance failure'
 $chat=Api '/api/chat/status';$dashboard=Api '/api/dashboard'
 Check ($dashboard.last_call_error.code-eq'insufficient_balance') 'management retains exact balance diagnosis'
 Check ($chat.offline-and!$chat.call_failed-and$null-eq$chat.call_error) 'balance failure automatically suspends desktop without conversation error'
 $null=Api '/api/control' @{offline=$true}
 Check (!(Api '/api/chat/status').call_failed-and$null-eq(Api '/api/chat/status').call_error) 'offline hides prior failure from conversation'
 Stop-Backend
 Write-Output "Offline process integration passed: $checks checks. Evidence: $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
}
