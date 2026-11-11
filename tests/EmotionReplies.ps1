param([Parameter(Mandatory)][string]$Executable,[string]$OldExecutable='',[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ('emotion-replies-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;HIBERNATION_SESSION_KEY=$env:HIBERNATION_SESSION_KEY};$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='emotion-replies-synthetic'
$service=$null;$desktop=$null;$provider=$null;$checks=@()
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks+=,$Name;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(35);while([DateTime]::UtcNow-lt$end){if($script:service-and$script:service.HasExited){throw 'emotion_fixture_exited'};if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer emotion-replies-synthetic'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Start-Backend($File){$script:desktop=[PrivateDesktop]::new($File,$script:configPath,$script:ready,$ProjectRoot);$script:service=$desktop.Child;Await {if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'ready';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(12000)) 'isolated state saves and exits';$desktop.Dispose();$script:desktop=$null;$script:service=$null}
function Count-Calls($Review=$false){$file=Join-Path $root $(if($Review){'provider/review-requests.jsonl'}else{'provider/requests.jsonl'});if(Test-Path -LiteralPath $file){return @(Get-Content -LiteralPath $file).Count};0}
function Plan($Reply){@{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$Reply})}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8NoBOM;@{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root provider/review-plan.json) -Encoding utf8NoBOM}
function Message($Reply){Plan $Reply;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='合成测试：这次小故事中的争执让我不高兴，但不需要骂人。'};Await {!(Api '/api/chat').busy} 'completed turn';Check (!(Api '/api/chat').call_failed) 'approved visible reply completes';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$result=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$result.trace_id}}
function Final($Feeling){@{version=1;kind='reply';text='我先缓一缓。';calls=@();feeling=$Feeling}}
try{
 $pr=Join-Path $root provider;New-Item -ItemType Directory -Force $pr|Out-Null;$provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$pr+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $pr port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $pr port))
 $character=Join-Path $root character;New-Item -ItemType Directory -Path $character|Out-Null
 @{schemaVersion=1;version=1;id='synthetic-emotion-role';displayName='Fixture R';notice='Literal synthetic validation only';identity='A fictional test role';attention=@('synthetic events');boundaries=@('Use only actual fixture evidence');character=@{identity='A fictional test role';voice=@('Short plain replies')}}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character persona.json) -Encoding utf8NoBOM
 @{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='Synthetic';description='Synthetic stage';next='';minimumEvidence=0})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $character stages.json) -Encoding utf8NoBOM
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.frontend.startupAnimation=$false;$cfg.plugins=@{};$cfg.computer=@{enabled=$false;allowScreenCapture=$false;readRoots=@()};$cfg.skills=@{enabled=$false;useBuiltins=$false;root=(Join-Path $root skills);grants=@()};$cfg.cognition=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Synthetic actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='review';label='Synthetic review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=0;retainConversationFocus=$false};$cfg.contextControl=@{enabled=$true;reviewSilence=$false;protocolRepairs=0}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8NoBOM
 if($OldExecutable){Start-Backend $OldExecutable;$null=Api '/api/control' @{paused=$true};$legacy=(Api '/api/state').continuous_state;Check ($legacy.version-eq3) 'migration starts from actual 0.8.1 state';Stop-Backend}
 Start-Backend $Executable;$migrated=(Api '/api/state').continuous_state
 if($OldExecutable){Check ($migrated.version-eq4-and[Math]::Abs($migrated.energy-$legacy.energy)-lt.002-and[Math]::Abs($migrated.mood-$legacy.mood)-lt.002) 'new process preserves existing energy and mood across version migration'}else{Check ($migrated.version-eq4) 'fresh process uses the current state schema'}
 Check ($migrated.affect.emotions.anger-eq0) 'old data does not manufacture anger';$null=Api '/api/control' @{paused=$false};$null=Api '/api/model' @{id='actor'}
 $actorBefore=Count-Calls;$reviewBefore=Count-Calls $true
 $first=Message (Final @{source_id='${event_id}';stimulus=@{emotions=@{anger=.8};desires=@{express=.3}}})
 @{source_event_id=$first.source_event_id;feeling_result=$first.feeling_result;validation=$first.validation;turn_count=$first.turns.Count;review_count=$first.reviews.Count}|ConvertTo-Json -Depth 25|Set-Content -LiteralPath (Join-Path $root first-feeling-receipt.json) -Encoding utf8NoBOM
 Check ($first.turns.Count-eq1-and$first.reviews.Count-eq1-and(Count-Calls)-eq$actorBefore+1-and(Count-Calls $true)-eq$reviewBefore+1) 'final feeling uses one actor and one review without an extra tool round'
 Check ($first.feeling_result.ok-and$first.feeling_result.result.changed-and$first.feeling_result.result.effect-eq'continuous_target_not_instant_state') 'approved sourced feeling has an actual accepted receipt'
 $early=(Api '/api/state').continuous_state.affect;Check ($early.emotions.anger-ge0-and$early.emotions.anger-lt.3-and$early.emotion_drive.anger-gt0) 'stimulus changes latent drive without an instant emotion setter'
 Start-Sleep -Milliseconds 1500;$grown=(Api '/api/state').continuous_state.affect;Check ($grown.emotions.anger-gt$early.emotions.anger) 'actual emotion advances gradually in the running process'
 $captured=(Get-Content -LiteralPath (Join-Path $pr requests.jsonl) -Raw)+(Get-Content -LiteralPath (Join-Path $pr review-requests.jsonl) -Raw)
 Check (!$captured.Contains('emotion_drive')-and!$captured.Contains('desire_drive')-and!$captured.Contains('last_stimulus_seconds')-and!$captured.Contains('clock_seconds')) 'model and reviewer inputs exclude private drives and affect source clocks'
 $knownSource=$first.source_event_id;$countBefore=@($grown.sources).Count
 $bad=Message (Final @{source_id='not-an-admitted-source';stimulus=@{emotions=@{joy=1}}})
 Check (!$bad.feeling_result.ok-and$bad.feeling_result.error.code-eq'emotion_source_not_available') 'unavailable source is rejected while the visible reply remains valid'
 Check (@((Api '/api/state').continuous_state.affect.sources).Count-eq$countBefore) 'rejected source does not enter the private stimulus ledger'
 $dedup=Message (Final @{source_id=$knownSource;stimulus=@{emotions=@{anger=.8};desires=@{express=.3}}})
 Check ($dedup.feeling_result.ok-and$dedup.feeling_result.result.deduplicated-and!$dedup.feeling_result.result.changed) 'same source returns its saved receipt without restimulation'
 $null=Api '/api/control' @{paused=$true};$paused=(Api '/api/state').continuous_state.affect;Start-Sleep -Milliseconds 500;$held=(Api '/api/state').continuous_state.affect
 Check (($paused|ConvertTo-Json -Depth 15 -Compress)-ceq($held|ConvertTo-Json -Depth 15 -Compress)) 'explicit pause freezes actual values, drives and active cooldown clock'
 Stop-Backend;Start-Backend $Executable;$restored=(Api '/api/state').continuous_state.affect
 Check (($restored|ConvertTo-Json -Depth 15 -Compress)-ceq($held|ConvertTo-Json -Depth 15 -Compress)) 'saved emotion and deduplication state survive a fresh process without absence simulation';Stop-Backend
 @{completed=$true;checks=$checks;source='literal_synthetic_only';real_api_calls=0;production_archive_read=$false;actual_old_binary_migration_tested=[bool]$OldExecutable;new_state_version=4;root=$root}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8NoBOM
 Write-Output "Emotion process evidence: $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(5000)}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
 foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
}
