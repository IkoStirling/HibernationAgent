param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/intention-restart-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs);$env:HIBERNATION_SESSION_KEY='life-fixture-only';$desktop=$null;$service=$null;$provider=$null;$checks=0
function Check($v,$name){if(!$v){throw "FAILED: $name"};$script:checks++;Write-Output "PASS $name"}
function Await([scriptblock]$probe,$name,[int]$Seconds=30){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $name"}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer life-fixture-only'};TimeoutSec=12};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 60 -Compress))};Invoke-RestMethod @p}
function Plan($items){@{nonce=[guid]::NewGuid().ToString('N');responses=@($items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Call($name,$arguments){@{version=1;kind='call';text=$null;calls=@(@{id=[guid]::NewGuid().ToString('N');tool=$name;args=$arguments})}}
function Reply{@{version=1;kind='reply';text='嗯。';calls=@();presentation=@{speech=$false;bubble=$false}}}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Message($items){Plan $items;$null=Api '/api/message' @{request_id=[guid]::NewGuid().ToString('N');text='独立合成经历：想泡杯茶，又想留一点空闲。'};Await {!(Api '/api/chat').busy} 'foreground turn';Check (!(Api '/api/chat').call_failed) 'foreground proposal accepted'}
function Feedback{$wire=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 80;$last=$wire.messages[-1].content;if($last-isnot[string]){$last=$last[0].text};($last|ConvertFrom-Json -Depth 70).tool_results[0]}
function Observe{Message @((Call 'self.observe' @{}),(Reply));(Feedback).result.threads}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'fixture exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown';$desktop.Dispose();$script:desktop=$null}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.backgroundBudget=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxToolRounds=6
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Life fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;autonomyPursuitCooldownSeconds=10;showBubble=$false;reactionCacheEnabled=$false};habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false;localGatherEnabled=$false};canteen=@{enabled=$true;automatic=$false};reading=@{enabled=$true;autonomousReading=$false}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 function World{@((Api '/api/capabilities').plugins|Where-Object id -eq 'habitat')[0].current}
 function Requests{@(Get-Content (Join-Path $root provider/requests.jsonl)).Count}
 Message @((Call 'self.notice' @{id='restart-own';expected_revision=0;kind='intention';topic='留一颗莓果在桌边';note='给这里添一个小东西。';stance='want';source_ids=@('${event_id}')}),(Reply))
 $pursue=@{commands=@(@{kind='pursue_intention';domain='desktop';intention=@{id='restart-own';revision=1}})}
 $responses=@(@{response=(Call 'activity.enqueue' $pursue)},@{response=(Reply)},@{response=(Call 'habitat.create' @{kind='berry';placement='nearby'})},@{response=(Silent);delay_ms=8000})
 @{nonce=[guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 60|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[guid]::NewGuid().ToString('N');text='独立合成经历：让这里多一点东西。'}
 Await {(Requests)-ge6-and(World).objects.Count-eq1-and$null-ne(Api '/api/activities/status').personal_pursuit.active} 'actual side effect and next inference in progress'
 $object=(World).objects[0].id;$count=Requests;$keyHash=(Get-FileHash (Join-Path $root data/runtime/memory-key.dpapi)).Hash
 # Allow the native event checkpoint in the next application tick; this is a
 # short crash fixture, not a long stability wait.
 Start-Sleep -Milliseconds 300;$oldPid=$service.Id;$service.Kill();$null=$service.WaitForExit(5000);$desktop.Dispose();$script:desktop=$null
 Start-Backend;Check ($service.Id-ne$oldPid) 'a new process owns the recovered archive'
 Await {$s=Api '/api/activities/status';$null-eq$s.personal_pursuit.active-and@($s.recent|Where-Object {$_.kind-eq'pursue_intention'-and$_.effect_outcome-eq'interrupted'}).Count-eq1} 'old short own-time session retired after restart'
 Check ((Requests)-eq$count) 'restarting does not automatically ask the model to replay the old short plan'
 Check ((World).objects.Count-eq1-and(World).objects[0].id-eq$object) 'the actual world effect survives without being duplicated'
 Check ((Get-FileHash (Join-Path $root data/runtime/memory-key.dpapi)).Hash-eq$keyHash) 'archive key is preserved'
 $threads=Observe;$thread=@($threads|Where-Object id -eq 'restart-own')[0];$attempt=@($thread.attempts|Where-Object {$_.action-eq'pursue_intention'-and$_.status-eq'interrupted'})
 Check ($attempt.Count-eq1-and$attempt[0].interruption_reason-eq'application_restarted'-and$attempt[0].result_sources.Count-eq1) 'partial tool evidence returns to the same intention despite the missing final model reply'
 Check ($thread.status-eq'active'-and$thread.revision-eq1) 'restart keeps the intention available for a fresh later choice'
 Check ((World).objects.Count-eq1) 'reading recovered progress creates no second object'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "Intention restart passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
