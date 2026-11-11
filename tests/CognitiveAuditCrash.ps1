param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot);Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/cognitive-audit-crash-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='audit-crash-fixture';$desktop=$null;$service=$null;$checks=0;$started=[DateTime]::UtcNow
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$until=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$until){if($service.HasExited){throw 'audit fixture exited'};if(&$Probe){return};Start-Sleep -Milliseconds 80};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer audit-crash-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @p}
function Start-Backend{
 if(Test-Path -LiteralPath $ready){Remove-Item -LiteralPath $ready}
 $script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$script:service=$desktop.Child;Await {Test-Path -LiteralPath $ready} startup;$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean audited shutdown';$desktop.Dispose();$script:desktop=$null;$script:service=$null}
try{
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.plugins=@{};$cfg.profiles=@($cfg.profiles[0]);$cfg.review=@{enabled=$false};$cfg.frontend.startupAnimation=$false;$cfg.cognition=@{enabled=$true};$cfg.solitudeIntervalMs=43200000;$cfg.idleBeforeSolitudeMs=43200000
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 30|Set-Content -LiteralPath $config -Encoding utf8NoBOM;Start-Backend
 $first=(Api '/api/dashboard').cognitive_trace;Check ($null-eq$first.previous_run_clean-and$first.audit_session_open) 'first audited session has no invented previous closure'
 $null=Api '/api/message' @{request_id='durable-original';text='SYNTHETIC_ORIGINAL_BEFORE_OWNED_CRASH'};Await {!(Api '/api/chat').busy} original_committed
 Check (@((Api '/api/chat').messages|Where-Object text -eq 'SYNTHETIC_ORIGINAL_BEFORE_OWNED_CRASH').Count-eq1) 'original is committed before an abrupt stop'
 # Kill only the retained fixture process, verified by its exact executable.
 Check ([IO.Path]::GetFullPath($service.Path)-eq[IO.Path]::GetFullPath((Join-Path $BuildRoot hibernation-agent.exe))) 'abrupt-stop target belongs to this fixture'
 $service.Kill();Check ($service.WaitForExit(10000)) 'owned fixture abruptly exits';$desktop.Dispose();$desktop=$null;$service=$null;Start-Backend
 $state=(Api '/api/dashboard').cognitive_trace;Check ($state.previous_run_clean-eq$false-and$state.audit_session_open) 'restart explicitly records the unsealed audit tail'
 Await {@((Api '/api/mind/trace').events|Where-Object {$_.kind-eq'cognition.runtime.started'-and$_.status-eq'recovered_after_unclean_exit'}).Count-eq1} recovery_receipt
 Check (@((Api '/api/chat').messages|Where-Object text -eq 'SYNTHETIC_ORIGINAL_BEFORE_OWNED_CRASH').Count-eq1) 'original archive survives without being replayed or duplicated'
 Check (((Api '/api/mind/trace')|ConvertTo-Json -Depth 14)-notmatch'SYNTHETIC_ORIGINAL_BEFORE_OWNED_CRASH') 'public recovery trace contains metadata only'
 Stop-Backend;Start-Backend;Check ((Api '/api/dashboard').cognitive_trace.previous_run_clean-eq$true) 'a subsequent normal seal is distinguishable from the abrupt exit'
 Check ((Api '/api/dashboard').health-eq'ok') 'recovered audit and application remain healthy';Stop-Backend
 @{completed=$true;checks=$checks;paid_model_calls=0;abrupt_fixture_stops=1;real_elapsed_seconds=([DateTime]::UtcNow-$started).TotalSeconds;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8NoBOM
 Write-Output "Cognitive audit crash checks passed: $checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){$service.Kill()}}};if($desktop){$desktop.Dispose()}}
