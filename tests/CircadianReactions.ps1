param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/circadian-reactions-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='circadian-reaction-fixture';$service=$null;$provider=$null;$desktop=$null;$checks=0;$started=[DateTime]::UtcNow
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=25){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if($service-and$service.HasExited){throw 'reaction sleep host exited'};if(&$Probe){return};Start-Sleep -Milliseconds 80};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer circadian-reaction-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))};Invoke-RestMethod @p}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
function Status{Api '/api/reactions/status'}
function Band{
 # Affect categories follow the circadian category in the cache key. Require
 # exactly one known circadian value rather than interpreting the final bool.
 $parts=@((Status).preparation_cadence.key|ConvertFrom-Json)
 $bands=@($parts|Where-Object {$_-is[string]-and$_-in@('settled','waking','sleeping')})
 if($bands.Count-ne1){throw 'reaction cache must contain exactly one circadian band'}
 return $bands[0]
}
function Plugin($Tool,$Arguments){$null=Api '/api/plugin/test' @{tool=$Tool;args=$Arguments};Await {$script:receipt=Api '/api/plugin/result';$script:receipt.state-in@('succeeded','failed')} $Tool;Check $script:receipt.result.ok $Tool;return $script:receipt.result.result}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $root provider/port)} provider;$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{version=1;reactions=@(@{text='先让我站稳一会儿。';animation='idle'},@{text='嗯，换个地方。';animation='curious'})}})}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8NoBOM
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=43200000;$cfg.idleBeforeSolitudeMs=43200000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Local sleep reaction fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$false;reactionCacheEnabled=$true;showBubble=$false};rhythm=@{enabled=$true;automaticRest=$false;circadianEnabled=$true;idleBeforeRestSeconds=5}}
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 35|Set-Content -LiteralPath $config -Encoding utf8NoBOM
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child;Await {Test-Path -LiteralPath $ready} startup;$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='actor'};$null=Api '/api/reactions/refill' @{};Await {(Status).available-eq2} initial_cache
 @{reaction_status=(Status);parsed_key=@((Status).preparation_cadence.key|ConvertFrom-Json);band_observed=(Band);requests=(Requests);rhythm_status=(Api '/api/rhythm/status')}|ConvertTo-Json -Depth 25|Set-Content -LiteralPath (Join-Path $root awake-cache-evidence.json) -Encoding utf8NoBOM
 Check ((Band)-eq'settled'-and(Requests)-eq1) 'awake preparation uses one local request and a settled cache band'
 $minute=(Get-Date).Hour*60+(Get-Date).Minute;$null=Plugin 'rhythm.configure' @{expected_revision=0;preferences=@{enabled=$true;start_minute=($minute+1439)%1440;end_minute=($minute+120)%1440;drag_wake_probability=0;message_wake_probability=0;inertia_seconds=30}}
 Await {(Api '/api/rhythm/status').phase-eq'sleeping'} sleep
 $before=Requests;$null=Api '/api/reactions/refill' @{};Start-Sleep -Seconds 12
 Check ((Requests)-eq$before-and(Api '/api/rhythm/status').phase-eq'sleeping') 'sleep prevents explicit refill from invoking even a local model'
 $null=Api '/api/rhythm/wake' @{};Await {(Band)-eq'waking'-and(Status).available-eq2} waking_cache
 Check ((Requests)-eq$before+1) 'waking invalidates pre-sleep candidates and prepares one replacement batch'
 $captureObject=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;$capture=($captureObject.messages|ForEach-Object content)-join"`n"
 Check ($capture-match'"circadian"'-and$capture-match'sleep_inertia') 'reaction generator receives actual sleep state without changing its stable system prompt'
 $before=Requests;Start-Sleep -Seconds 5
 Check ((Requests)-eq$before-and(Band)-eq'waking') 'changing continuous inertia does not invalidate the cache every tick'
 Await {(Band)-eq'settled'-and(Status).available-eq2} settled_cache 45
 Check ((Requests)-eq$before+1) 'settled transition performs one bounded replacement instead of retaining waking candidates'
 Check ((Api '/api/dashboard').health-eq'ok'-and@((Api '/api/chat').messages).Count-eq0) 'background preparations stay outside the visible dialogue'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'isolated native process checkpoints and stops'
 @{completed=$true;checks=$checks;paid_model_calls=0;local_requests=(Requests);real_elapsed_seconds=([DateTime]::UtcNow-$started).TotalSeconds;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8NoBOM
 Write-Output "Circadian reaction checks passed: $checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
