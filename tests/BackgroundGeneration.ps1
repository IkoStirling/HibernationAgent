param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/background-generation-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='background-generation-fixture';$service=$null;$provider=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=20){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer background-generation-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{version=1;reactions=@()}},@{response=@{version=1;commands=@()};delay_ms=1800},@{response=@{version=1;reactions=@(@{text='让我在这里站一会儿。';animation='idle'})}})}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Background fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;autonomyRefillSeconds=30;reactionCacheEnabled=$true;showBubble=$false}}
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $config -Encoding utf8;$desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child
 Await {if($service.HasExited){throw 'background fixture exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'};$null=Api '/api/reactions/refill' @{}
 Await {(Api '/api/reactions/status').preparation_cadence.quiet-eq1} 'first quiet reaction choice';Check ((Requests)-eq1) 'first short reaction choice is a separate actual request'
 Await {(Api '/api/activities/status').generating-and(Requests)-eq2} 'actual autonomous planning request' 45
 $null=Api '/api/reactions/refill' @{};$watch=[Diagnostics.Stopwatch]::StartNew();$overlap=$false
 while($watch.ElapsedMilliseconds-lt650){if((Api '/api/reactions/status').generating){$overlap=$true};Start-Sleep -Milliseconds 30}
 Check (!$overlap-and(Requests)-eq2) 'manual refill stays pending while autonomous language planning is in flight'
 Await {(Api '/api/reactions/status').available-eq1} 'pending short reaction follows planning'
 Check ((Requests)-eq3-and(Api '/api/activities/status').planning_cadence.quiet-eq1) 'pending preparation runs once after the first batch completes'
 Check ((Api '/api/chat').messages.Count-eq0-and!(Api '/api/activities/status').generating) 'both background batches remain private and finish without forced dialogue'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'background fixture stops cleanly';$desktop.Dispose();@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Background generation passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
