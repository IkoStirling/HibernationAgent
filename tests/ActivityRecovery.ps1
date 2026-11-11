param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$SeedExecutable,[string]$ScratchRoot='E:/Aliyat/Validation/ActivityRecovery',[ValidateSet('all','hourly','review-budget','wake','balance','starvation','failed-timer','cache-budget')][string]$Case='all',[switch]$ObserveOnly)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ((Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='synthetic-activity-recovery';$env:HA_RECOVERY_BALANCE='balance-actor-fixture';$checks=0;$service=$null;$provider=$null;$desktop=$null;$observations=@()
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Now{[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=40){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if($script:service-and$script:service.HasExited){throw 'recovery_host_exited'};if(& $Probe){return};Start-Sleep -Milliseconds 75};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer synthetic-activity-recovery'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Count-Requests($Review=$false){$path=Join-Path $script:trial $(if($Review){'provider/review-requests.jsonl'}else{'provider/requests.jsonl'});if(Test-Path -LiteralPath $path){return @(Get-Content -LiteralPath $path).Count};0}
function Plugin($Tool,$Arguments){$null=Api '/api/plugin/test' @{tool=$Tool;args=$Arguments};Await {$r=Api '/api/plugin/result';$r.state-in@('succeeded','failed')} $Tool;return (Api '/api/plugin/result').result}
function Stop-Backend{if(!$service){return};$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'isolated recovery host exits cleanly';$desktop.Dispose();$script:service=$null;$script:desktop=$null}
try{
 foreach($mode in $(if($Case-eq'all'){@('hourly','review-budget','wake','balance','starvation','failed-timer','cache-budget')}else{@($Case)})){
  $script:trial=Join-Path $root $mode;New-Item -ItemType Directory -Path $trial -Force|Out-Null
  Set-Content -LiteralPath (Join-Path $trial .activity-recovery-fixture) -Value 'hibernation.activity-recovery.v1' -Encoding utf8NoBOM
  $seedMode=if($mode-eq'starvation'){'review-budget'}else{$mode};$seedText=& $SeedExecutable $trial $seedMode;if($LASTEXITCODE-ne0){throw 'seed_failed'};$seed=$seedText|ConvertFrom-Json
  $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$trial+'/provider"') -WindowStyle Hidden -PassThru
  Await {Test-Path -LiteralPath (Join-Path $trial provider/port)} provider;$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $trial provider/port))
  @{nonce='quiet-synthetic';responses=@(@{response=@{version=1;commands=@()}})}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $trial provider/plan.json) -Encoding utf8NoBOM
  if($mode-eq'starvation'){@{nonce='continuous-finite-waits';responses=@(@{response=@{version=1;commands=@(1..4|ForEach-Object {@{kind='wait';seconds=10}})}})}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $trial provider/plan.json) -Encoding utf8NoBOM}
  @{nonce='approve-synthetic';responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $trial provider/review-plan.json) -Encoding utf8NoBOM
  if($mode-eq'balance'){@{is_available=$false;balance_infos=@(@{currency='CNY';total_balance='0.00'})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $trial provider/balance.json) -Encoding utf8NoBOM}
  $character=Join-Path $trial character;New-Item -ItemType Directory -Path $character|Out-Null
  @{schemaVersion=1;version=1;id='activity-recovery-synthetic';displayName='Synthetic Activity Role';notice='Synthetic fixture only';identity='A fictional test role';attention=@('synthetic timing');boundaries=@('Only actual local fixture receipts are facts.');character=@{identity='A fictional test role';attention=@('synthetic timing');voice=@('Plain short responses.')}}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $character persona.json) -Encoding utf8NoBOM
  @{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='Synthetic';description='Synthetic only';next='';minimumEvidence=0})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $character stages.json) -Encoding utf8NoBOM
  $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
  $cfg.dataRoot=Join-Path $trial data;$cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.cognition=@{enabled=$false};$cfg.reflectOnActivities=$false;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
  $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Synthetic Actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;timeoutMs=10000},@{id='reviewer';label='Synthetic Reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true;timeoutMs=10000})
  if($mode-eq'balance'){$cfg.profiles[1].model='balance-fixture';$cfg.profiles[1].credentialEnvironment='HA_RECOVERY_BALANCE'}
  $cfg.review=@{enabled=($mode-eq'review-budget');profileId='reviewer';maxRewrites=0};$cfg.backgroundBudget=@{enabled=$true;maxCallsPerDay=$(if($mode-eq'review-budget'){1}else{100});maxInputTokensPerDay=100000}
  $cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;autonomyRefillSeconds=30;reactionCacheEnabled=$false;showBubble=$false};rhythm=@{enabled=($mode-in@('wake','starvation'));circadianEnabled=$true;automaticRest=$false;idleBeforeRestSeconds=5}}
  if($mode-in@('failed-timer','cache-budget')){$cfg.plugins.widget.autonomousMode=$false}
  $config=Join-Path $trial config.json;$ready=Join-Path $trial ready.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $config -Encoding utf8NoBOM
  $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child
  Await {if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} backend
  $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
  if($mode-notin@('failed-timer','cache-budget')){Await {(Api '/api/activities/status').next_planning_ms-gt0} 'activity clock initialized'}
  if($mode-eq'cache-budget'){
   $b=Api '/api/background-budget'
   Check ($b.accounting_basis-eq'uncached_input_tokens'-and$b.input_tokens-eq100-and$b.total_input_tokens-eq1200-and$b.cached_input_tokens-eq1100) 'old accounting is reclassified only from the matching cache history window'
   Check ($b.used_calls-eq14-and$b.reported_calls-eq12-and$b.accounting_migration.cache_split_recovered-and$b.accounting_migration.calls_preserved) 'cache migration preserves actual reservations and old reporting counts'
   Check ((Count-Requests)-eq0) 'accounting migration makes no provider request'
   $observations+=@{case=$mode;budget=$b}
  }elseif($mode-eq'failed-timer'){
   Await {@((Api '/api/state').jobs|Where-Object {$_.id-eq'solitude.timer'-and$_.state-eq'pending'}).Count-eq1} 'failed periodic solitude is armed again' 12
   $timer=@((Api '/api/state').jobs|Where-Object id -eq 'solitude.timer')[0]
   Check ($timer.due_at-gt(Now)+60000-and$timer.run_token-ge1) 'repair schedules one future opportunity and preserves execution lineage'
   Check ((Count-Requests)-eq0-and(Api '/api/background-budget').used_calls-eq0) 'repair does not replay missed intervals or call a model'
   $observations+=@{case=$mode;timer=$timer;actor_calls=(Count-Requests)}
  }elseif($mode-eq'hourly'){
   Await {(Now)-ge($seed.hourly_resets_ms-9000)} 'pre-reset observation' 45
   $before=Api '/api/activities/status';$budget=Api '/api/background-budget';$calls=Count-Requests
   if(!$ObserveOnly){Check ($before.planning_deferred_reason-eq'hourly_reserve'-and$before.planning_cadence.failures-eq0-and$before.last_error-eq''-and$calls-eq0) 'hourly reserve defers without provider calls or failed planning records'}
   Await {(Now)-ge($seed.hourly_resets_ms+1000)} 'real hourly reset' 15
   $limit=[Math]::Max(1,[int][Math]::Ceiling(($seed.hourly_resets_ms+10000-(Now))/1000))
   if(!$ObserveOnly){Await {(Count-Requests)-ge1} 'automatic post-hourly refill' $limit}
   else{Start-Sleep -Seconds 2}
   $after=Api '/api/activities/status';$observations+=@{case=$mode;before=$before;after=$after;actor_calls=(Count-Requests);seed=$seed;observed_ms=(Now)}
   if(!$ObserveOnly){Check ($after.planning_cadence.failures-eq0) 'hourly recovery keeps genuine failure count unchanged'}
  }elseif($mode-eq'review-budget'){
   Await {(Api '/api/background-budget').used_calls-eq1-and!(Api '/api/activities/status').generating} 'review reservation denied after actor call' 45
   $before=Api '/api/activities/status';$used=(Api '/api/background-budget').used_calls
   if(!$ObserveOnly){Check ($before.planning_cadence.failures-eq0-and$before.last_error-eq''-and(Count-Requests)-eq1-and(Count-Requests $true)-eq0) 'review budget race remains a wait rather than a fake model failure'}
   $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.backgroundBudget.maxCallsPerDay=3;$null=Api '/api/settings' @{settings=$settings};$changed=Now
   if(!$ObserveOnly){Await {(Count-Requests)-ge2-and(Count-Requests $true)-eq1-and!(Api '/api/activities/status').generating} 'budget-ready edge refills reviewed queue' 10}
   else{Start-Sleep -Seconds 3}
   $after=Api '/api/activities/status';$daily=Api '/api/background-budget';$observations+=@{case=$mode;before=$before;after=$after;used_before=$used;budget_after=$daily;resumed_after_ms=((Now)-$changed);actor_calls=(Count-Requests);review_calls=(Count-Requests $true)}
   if(!$ObserveOnly){Check ($daily.used_calls-eq3-and$after.planning_cadence.failures-eq0-and$after.planning_cadence.quiet-eq1) 'recovery preserves spent budget and obtains a fresh approved quiet choice'}
  }elseif($mode-eq'wake'){
   $initial=Api '/api/activities/status';Check ($initial.next_planning_ms-(Now)-gt60000) 'synthetic quiet cadence is deliberately long before rest'
   $m=(Get-Date).Hour*60+(Get-Date).Minute;$preferences=@{enabled=$true;start_minute=($m+1439)%1440;end_minute=($m+60)%1440;drag_wake_probability=0;message_wake_probability=0;inertia_seconds=600}
   $configured=Plugin 'rhythm.configure' @{expected_revision=0;preferences=$preferences};Check $configured.ok 'actual versioned rest window configured'
   Await {(Api '/api/dashboard').circadian.phase-eq'sleeping'} 'confirmed actual sleep' 25
   $sleep=Api '/api/activities/status';$calls=Count-Requests;Start-Sleep -Milliseconds 1200
   Check ((Count-Requests)-eq$calls) 'actual sleeping phase makes no model request'
   $woke=Plugin 'rhythm.wake' @{};Check $woke.ok 'wake uses real plugin receipt';$wakeMs=Now
   if(!$ObserveOnly){Await {(Count-Requests)-ge1-and!(Api '/api/activities/status').generating} 'fresh consideration after actual wake' 10}
   else{Start-Sleep -Seconds 3}
   $after=Api '/api/activities/status';$observations+=@{case=$mode;before=$initial;sleep=$sleep;after=$after;actor_calls=(Count-Requests);wake_to_observation_ms=((Now)-$wakeMs)}
   if(!$ObserveOnly){Check ($after.planning_cadence.quiet-eq1-and$after.planning_cadence.failures-eq0-and(Api '/api/dashboard').circadian.phase-eq'idle') 'wake starts one fresh consideration without old quiet streak or imagined rest completion'}
  }elseif($mode-eq'balance'){
   Await {(Api '/api/dashboard').balance_control.balance_offline} 'real local zero-balance gate'
   Check ((Count-Requests)-eq0-and(Api '/api/background-budget').used_calls-eq0) 'balance polling does not spend model budget or generate activity'
   $null=Api '/api/control' @{offline=$true};$before=Api '/api/activities/status'
   @{is_available=$true;balance_infos=@(@{currency='CNY';total_balance='1.00'})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $trial provider/balance.json) -Encoding utf8NoBOM
   Await {((Now)-(Api '/api/dashboard').balance_control.last_attempt_ms)-ge5100} 'unpaid balance query cooldown' 10
   $null=Api '/api/balance/check' @{};Await {!((Api '/api/dashboard').balance_control.checking)} 'positive balance receipt'
   $control=(Api '/api/dashboard').balance_control;Check (!$control.balance_offline-and$control.manual_offline-and(Count-Requests)-eq0) 'funding clears only automatic offline and honors manual pause'
   $null=Api '/api/control' @{offline=$false};$resumed=Now
   Await {(Count-Requests)-ge1-and!(Api '/api/activities/status').generating} 'activity resumes after real balance recovery' 12
   $after=Api '/api/activities/status';$daily=Api '/api/background-budget';$observations+=@{case=$mode;before=$before;after=$after;budget_after=$daily;resumed_after_ms=((Now)-$resumed);actor_calls=(Count-Requests)}
   Check ($daily.used_calls-eq1-and$after.planning_cadence.failures-eq0-and$after.planning_cadence.quiet-eq1) 'balance recovery resumes one genuine planner call without resetting allowance'
  }else{
   Await {$p=Api '/api/activities/status';$p.queued-ge2-and$null-ne$p.active-and!$p.generating} 'real finite activity queue running' 45
   $before=Api '/api/activities/status';$originalActive=$before.active.id;$requests=Count-Requests
   $m=(Get-Date).Hour*60+(Get-Date).Minute;$preferences=@{enabled=$true;start_minute=($m+1439)%1440;end_minute=($m+60)%1440;drag_wake_probability=0;message_wake_probability=0;inertia_seconds=600}
   $configured=Plugin 'rhythm.configure' @{expected_revision=0;preferences=$preferences};Check $configured.ok 'due rest window configured while finite approved queue exists'
   $samples=@();$until=[DateTime]::UtcNow.AddSeconds(50);$slept=$false
   while([DateTime]::UtcNow-lt$until){$p=Api '/api/activities/status';$dashboard=Api '/api/dashboard';$phase=$dashboard.circadian.phase;$samples+=@{observed_ms=(Now);phase=$phase;queued=$p.queued;active=($null-ne$p.active);active_id=$p.active.id;actor_calls=(Count-Requests);deferred=$p.planning_deferred_reason;background_allowed=$dashboard.runtime_status.background_allowed;background_defer_reason=$dashboard.runtime_status.background_defer_reason;runtime_code=$dashboard.runtime_status.code};if($phase-eq'sleeping'){$slept=$true;break};Start-Sleep -Milliseconds 500}
   $after=Api '/api/activities/status';$observations+=@{case=$mode;before=$before;after=$after;slept=$slept;actor_calls_before=$requests;actor_calls_after=(Count-Requests);samples=$samples}
   if(!$ObserveOnly){Check $slept 'rest admission stops replenishing and actual finite queue drains before sleep';Check ((Count-Requests)-eq$requests) 'due rest window makes no new planner request';Check (@($after.recent|Where-Object {$_.id-eq$originalActive-and$_.status-eq'completed'}).Count-eq1) 'already approved active wait completes naturally rather than being cancelled or invented';$wire=Get-Content -LiteralPath (Join-Path $trial provider/last-request.json) -Raw;Check (!$wire.Contains('circadian_admission_ready')) 'host admission metadata is absent from actual character model input';$waiting=@($samples|Where-Object background_defer_reason -eq 'circadian_window');Check ($waiting.Count-gt0-and@($waiting|Where-Object background_allowed -eq $true).Count-eq0) 'dashboard identifies the real routine wait and disables new background planning'}
  }
  Check ((Api '/api/dashboard').health-eq'ok') 'synthetic host remains healthy';Stop-Backend
  if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id};$provider=$null
 }
 @{completed=$true;observe_only=[bool]$ObserveOnly;checks=$checks;root=$root;observations=$observations;real_model_calls=0;production_archive_accessed=$false}|ConvertTo-Json -Depth 50|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8NoBOM
 Write-Output "Activity recovery evidence: $root"
}catch{
 @{completed=$false;failed_case=$mode;failure_message=$_.Exception.Message;checks=$checks;root=$root;observations=$observations;real_model_calls=0;production_archive_accessed=$false}|ConvertTo-Json -Depth 50|Set-Content -LiteralPath (Join-Path $root failure.json) -Encoding utf8NoBOM
 throw
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
