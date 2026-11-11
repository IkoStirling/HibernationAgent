param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $BuildRoot ('integration/continuity-mind-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=Join-Path $root tmp;$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null;$env:HIBERNATION_SESSION_KEY='mind-synthetic-fixture'
$service=$null;$desktop=$null;$provider=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer mind-synthetic-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 70 -Compress))};Invoke-RestMethod @p}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Message($Items){
 $responses=@((Calls $Items),(Silent));@{nonce=[Guid]::NewGuid().ToString('N');responses=@($responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 70|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='SYNTHETIC_ONLY 先放一会，再继续未开始的活动。'};Await {!(Api '/api/chat').busy} 'turn'
 if((Api '/api/chat').call_failed){throw 'fixture turn failed'}
 $capture=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 90
 $script:feedback=@(($capture.messages[-1].content|ConvertFrom-Json -Depth 80).tool_results)
}
function Observe{Message @((Tool observe mind.observe @{}));$script:feedback[0].result}
function Start-Backend{
 $script:desktop=[PrivateDesktop]::new($Executable,(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child
 Await {if($script:service.HasExited){throw 'fixture backend exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$script:service.Id}} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};if(!$service.WaitForExit(10000)){throw 'shutdown timeout'};$desktop.Dispose();$script:desktop=$null}
try{
 $provider=Start-Process (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Await {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false
 $cfg.cognition=@{enabled=$true;attention=$true;observations=$true;dwellMs=5000;resumeMinutes=15;observationCooldownMs=120000}
 $cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';size=168;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;roam=$false;respectSystemMotion=$false;showBubble=$false;reactionCacheEnabled=$false}}
 $cfg.contextControl=@{enabled=$true;preset='balanced';sessionProjection=$false};$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Mind fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='actor2';label='Second fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg|ConvertTo-Json -Depth 60|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 Check ((Api '/api/mind/status').enabled) 'runtime enables continuity without a second model'
 Check ((Api '/api/capabilities').tools.tool-contains'mind.observe') 'role discovers its working-state tools'
 Message @((Tool note mind.interpret @{id='hypothesis';expected_revision=0;text='PRIVATE_MIND_SENTINEL 也许它只是暂时想停一下';confidence='tentative';state='active';source_ids=@('${event_id}')}),(Tool memory memory.remember @{kind='conversation';text='SYNTHETIC_SOURCE_MARKER';source_ids=@('${event_id}')}))
 Check ($feedback[0].ok-and$feedback[0].result.revision-eq1-and!$feedback[0].result.fact) 'sourced hypothesis is versioned and explicitly not fact';$memory=$feedback[1].result.id
 $view=Observe;Check ($view.interpretations.Count-eq1-and$view.interpretations[0].text-like'PRIVATE_MIND_SENTINEL*') 'next conversation can use its own tentative interpretation'
 Message @((Tool stale mind.interpret @{id='hypothesis';expected_revision=0;text='stale';confidence='uncertain';state='active';source_ids=@('${event_id}')}));Check ($feedback[0].error.code-eq'interpretation_revision') 'stale interpretation update is rejected'
 Message @((Tool invented mind.interpret @{id='invented';expected_revision=0;text='invented';confidence='tentative';state='active';source_ids=@('not-real')}));Check (!$feedback[0].ok) 'fabricated source cannot produce interpretation'
 $null=Api '/api/access' @{id=$memory;accessible=$false};$view=Observe;Check ($view.interpretations.Count-eq0) 'revoking source access removes its interpretation from context'
 $null=Api '/api/access' @{id=$memory;accessible=$true};$view=Observe;Check ($view.interpretations.Count-eq1) 'restoring access recovers original interpretation without rewriting it'
 Message @((Tool change mind.interpret @{id='hypothesis';expected_revision=1;text='PRIVATE_MIND_SENTINEL 换一个暂定看法';confidence='uncertain';state='active';source_ids=@('${event_id}')}));Check ($feedback[0].result.revision-eq2) 'correction advances revision and retains history'
 Message @((Tool queue activity.enqueue @{commands=@(@{kind='wait';seconds=60},@{kind='wait';seconds=2;purpose='RESUME_PENDING_SENTINEL'})}));Check ($feedback[0].ok-and!$feedback[0].result.completed) 'queue acceptance is not completion'
 Await {(Api '/api/activities/status').active.kind-eq'wait'} 'first command starts'
 $started=[DateTime]::UtcNow;$view=Observe;Check (([DateTime]::UtcNow-$started).TotalSeconds-lt5) 'foreground interruption does not wait for old activity'
 $pending=@($view.suspended|Where-Object {$_.commands.Count-gt0})[-1];Check ($pending.commands.Count-eq1-and$pending.commands[0].purpose-eq'RESUME_PENDING_SENTINEL'-and$pending.requires_new_plan) 'interruption keeps pending command and excludes already-started operation'
 $id=$pending.id;$rev=$pending.revision;Message @((Tool resume activity.resume @{id=$id;expected_revision=$rev;decision='continue'}));Check ($feedback[0].ok-and$feedback[0].result.accepted-eq1) 'explicit continuation revalidates and queues only unstarted command'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.status-eq'completed'-and$_.purpose-eq'RESUME_PENDING_SENTINEL'}).Count-eq1} 'resumed action actually completes'
 Message @((Tool repeat activity.resume @{id=$id;expected_revision=$rev;decision='continue'}));Check (!$feedback[0].ok-and$feedback[0].error.code-eq'resume_stale_or_expired') 'same resume cannot execute twice'
 Message @((Tool queue2 activity.enqueue @{commands=@(@{kind='wait';seconds=60},@{kind='wait';seconds=3;purpose='RESTART_PENDING_SENTINEL'})}));Await {(Api '/api/activities/status').active.kind-eq'wait'} 'restart fixture running';$view=Observe;$pending=@($view.suspended|Where-Object {$_.commands.Count-gt0})[-1]
 Stop-Backend;Start-Backend;$null=Api '/api/model' @{id='actor2'};$view=Observe;Check (@($view.suspended|Where-Object id -eq $pending.id).Count-eq1) 'pending intention survives process restart and model change'
 Check ($view.interpretations[0].revision-eq2) 'interpretation survives model change independent of model history'
 Check ((Api '/api/activities/status').queued-eq0) 'restart never automatically replays suspended work'
 Message @((Tool abandon activity.resume @{id=$pending.id;expected_revision=$pending.revision;decision='abandon'}));Check ($feedback[0].result.abandoned) 'role can abandon instead of continuing'
 Message @((Tool withdraw mind.interpret @{id='hypothesis';expected_revision=2;text='PRIVATE_MIND_SENTINEL 暂时撤回';confidence='uncertain';state='withdrawn';source_ids=@('${event_id}')}));$view=Observe;Check ($view.interpretations.Count-eq0) 'withdrawn interpretation disappears without deleting its history'
 Check ($view.withdrawn_interpretations.Count-eq1-and!$view.PSObject.Properties['source_bindings']) 'withdrawal tombstone is explicit and native source bindings stay outside model input'
 Check ([Text.Encoding]::UTF8.GetByteCount(($view|ConvertTo-Json -Depth 80 -Compress))-le8192) 'working-state context stays within its independent byte limit'
 $diagnostic=Api '/api/mind/status';Check (!($diagnostic|ConvertTo-Json -Depth 30).Contains('PRIVATE_MIND_SENTINEL')) 'operator status contains counters without private judgments'
 Start-Sleep -Milliseconds 650;$audit=Api '/api/mind/trace';$public=$audit.events|ConvertTo-Json -Depth 20 -Compress
 Check (!$audit.private_content_included-and$public-notmatch'PRIVATE_MIND_SENTINEL|RESUME_PENDING_SENTINEL|source_ids|commands') 'execution trace contains only public identifiers and status'
 Check (@($audit.events|Where-Object kind -eq 'cognition.interpretation.changed').Count-ge3) 'accepted judgment changes and withdrawal leave durable audit receipts'
 Check (@($audit.events|Where-Object kind -eq 'cognition.activity.queued').Count-ge1-and@($audit.events|Where-Object kind -eq 'cognition.activity.started').Count-ge1) 'queue acceptance and execution start are separate trace events'
 $routed=(Api '/api/events' @{channel='agent.cognition'}).events;$linked=@($audit.events|Where-Object correlation);$mismatched=@($linked|Where-Object {$row=$_;@($routed|Where-Object {$_.event_id-eq$row.id-and$_.correlation-eq$row.correlation}).Count-ne1});Check ($linked.Count-gt0-and$mismatched.Count-eq0) 'existing virtual event channel preserves the original causal correlation identifiers'
 $page=Api '/api/mind/trace' @{limit=2};Check ($page.events.Count-eq2-and$page.next_before_id-eq$page.events[-1].id) 'bounded trace pagination returns an explicit cursor'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 70|ConvertFrom-Json -AsHashtable;$settings.cognition.enabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Check (!(Api '/api/mind/status').enabled-and!((Api '/api/capabilities').tools.tool-contains'mind.observe')) 'hot disable restores original tool surface'
 Stop-Backend
 $bytes=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)));Check (!$bytes.Contains('PRIVATE_MIND_SENTINEL')-and!$bytes.Contains('RESUME_PENDING_SENTINEL')) 'interpretations and suspended purposes remain encrypted on disk'
 @{completed=$true;checks=$checks;paid_model_calls=0;root=$root}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Continuity integration passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
