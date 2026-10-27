param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/autonomy-cadence-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='cadence-fixture-only';$service=$null;$provider=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Now{[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=25){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 70};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer cadence-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Response){@{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$Response})}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'cadence host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'cadence host stops cleanly';$desktop.Dispose()}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Cadence fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;autonomyRefillSeconds=30;reactionCacheEnabled=$false;showBubble=$false};habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;maxObjects=4}}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8;Plan @{version=1;commands=@()};Start-Backend;$null=Api '/api/model' @{id='actor'}
 Await {(Api '/api/activities/status').planning_cadence.quiet-eq1} 'first actual empty activity choice' 45
 $status=Api '/api/activities/status';$remaining=$status.next_planning_ms-(Now)
 Check ($status.planning_cadence.failures-eq0-and$status.last_error-eq''-and$status.queued-eq0-and$remaining-gt45000-and$remaining-le65000) 'successful empty choice is quiet time with a real doubled interval rather than a generation error'
 $before=Requests;Check ($before-eq1) 'one actual local planning request produced the empty choice'
 $capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;$scene=$capture.messages[1].content|ConvertFrom-Json;$time=$scene.time_context
 $sameInstant=([DateTimeOffset]$time.utc_time).ToUnixTimeSeconds()-eq[Math]::Floor($time.now_ms/1000)
 Check ($time.formatted_time_available-and$time.clock_source-eq'host_os'-and$time.host_local_hour-ge0-and$time.host_local_hour-lt24-and$sameInstant) 'autonomous choice receives readable time from its same authoritative timestamp without guessing user location'
 Stop-Backend;Start-Backend;Await {(Api '/api/activities/status').next_planning_ms-gt(Now)} 'restored planning clock';$status=Api '/api/activities/status'
 Check ($status.planning_cadence.quiet-eq1-and$status.next_planning_ms-(Now)-gt15000-and(Requests)-eq$before) 'restart preserves quiet backoff without asking the model again'
 $null=Api '/api/plugin/test' @{tool='habitat.create';args=@{kind='berry'}};Await {(Api '/api/plugin/result').state-in@('succeeded','failed')} 'new object';Check ((Api '/api/plugin/result').result.ok) 'real new desktop stimulus appears'
 Await {(Api '/api/activities/status').planning_cadence.quiet-eq0} 'stimulus resets cadence';$status=Api '/api/activities/status'
 Check ($status.planning_cadence.failures-eq0-and$status.next_planning_ms-(Now)-le31000-and(Requests)-eq$before) 'new object wakes future consideration without an immediate extra model call'
 Plan @{version=1;kind='silence';text=$null;calls=@()};$null=Api '/api/message' @{request_id='cadence-user-input';text='只检查一下，先不用回应。'};Await {!(Api '/api/chat').busy} 'new input';Check (!(Api '/api/chat').call_failed-and(Api '/api/activities/status').planning_cadence.quiet-eq0) 'foreground dialogue continues normally while quiet cadence resets'
 Stop-Backend;@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Autonomy cadence passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
