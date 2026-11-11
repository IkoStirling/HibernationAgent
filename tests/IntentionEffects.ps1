param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/intention-effects-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
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
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;autonomyPursuitCooldownSeconds=10;showBubble=$false;reactionCacheEnabled=$false};habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false;localGatherEnabled=$false};canteen=@{enabled=$true;automatic=$false}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 $note=@{id='tea-effects';expected_revision=0;kind='intention';topic='留一杯茶给自己';note='想看看茶最后的样子，不急着喝。';stance='want';source_ids=@('${event_id}')}
 Message @((Call 'self.notice' $note),(Reply))
 $pursue=@{commands=@(@{kind='pursue_intention';domain='kitchen';intention=@{id='tea-effects';revision=1}})}
 Message @((Call 'activity.enqueue' $pursue),(Reply),(Call 'canteen.cook' @{recipe_id='tea'}),(Silent))
 Await {$s=Api '/api/activities/status';$null-eq$s.personal_pursuit.active-and@($s.recent|Where-Object {$_.kind-eq'pursue_intention'-and$_.effect_outcome-eq'acted'}).Count-eq1} 'private kitchen step ended'
 Message @((Call 'body.observe' @{}),(Reply));$pending=(Feedback).result.personal_pursuit;Check ($pending.phase-eq'waiting_for_result'-and!$pending.goal_completed) 'accepted cooking remains waiting rather than completed'
 $batch=$pending.batch_id;Check ([bool]$batch) 'pending body binds an actual batch id'
 $job=@((Api '/api/state').jobs|Where-Object id -eq 'autonomy.personal-pursuit')[0];$trace=Api '/api/trace' @{id=('trace-autonomy.personal-pursuit-'+$job.run_token)};$accepted=@($trace.tool_results|Where-Object tool -eq 'canteen.cook')[0].source_id;Check ([bool]$accepted) 'actual acceptance source is retained'

 Stop-Backend;Start-Backend;Message @((Call 'body.observe' @{}),(Reply));Check ((Feedback).result.personal_pursuit.batch_id-eq$batch) 'pending cause survives encrypted restart'
 Await {$k=@((Api '/api/capabilities').plugins|Where-Object id -eq 'canteen')[0].current.current;$k.ready_count-eq1} 'actual tea finished' 30
 Message @((Call 'body.observe' @{}),(Reply));Check ((Feedback).result.personal_pursuit.phase-eq'result_available') 'actual ready event changes the offered occupation'
 $threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-effects')[0];$result=@($thread.attempts|Where-Object {$_.action-eq'kitchen_result'-and$_.status-eq'ready'})
 Check ($result.Count-eq1-and$result[0].batch_id-eq$batch-and$result[0].result_sources.Count-eq1) 'ready outcome returns to the exact original intention with a real source'
 Check ($thread.status-eq'active'-and$thread.revision-eq1) 'finishing cooking does not close or rewrite the whole intention'
 Message @((Call 'memory.remember' @{kind='inference';text='独立合成经历：检查料理受理来源的访问控制。';source_ids=@($accepted)}),(Reply));$accessMemory=(Feedback).result.id;Check ([bool]$accessMemory) 'original acceptance can be controlled independently of the later world event'
 $null=Api '/api/access' @{id=$accessMemory;accessible=$false};$threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-effects')[0];Check ($thread.status-eq'active'-and@($thread.attempts|Where-Object {$_.action-eq'kitchen_result'-and$_.status-eq'ready'}).Count-eq0) 'hidden acceptance cannot leak its causal association through a later ready event'
 Message @((Call 'body.observe' @{}),(Reply));Check ($null-eq(Feedback).result.personal_pursuit) 'hidden acceptance also removes the derived current occupation'
 $null=Api '/api/access' @{id=$accessMemory;accessible=$true};$threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-effects')[0];Check (@($thread.attempts|Where-Object {$_.action-eq'kitchen_result'-and$_.status-eq'ready'}).Count-eq1) 'restored source reveals the same actual outcome without replaying cooking'

 Message @((Call 'canteen.take' @{batch_id=$batch}),(Reply));Await {$k=@((Api '/api/capabilities').plugins|Where-Object id -eq 'canteen')[0].current.current;$k.ready_count-eq0} 'actual serving completed'
 Message @((Call 'body.observe' @{}),(Reply));Check ((Feedback).result.personal_pursuit.phase-eq'on_desktop') 'serving is not mistaken for consumption'
 $world=@((Api '/api/capabilities').plugins|Where-Object id -eq 'habitat')[0].current;$item=@($world.objects|Where-Object {$_.provenance.batch_id-eq$batch})[0];Check ([bool]$item.id) 'real served object exists'
 Message @((Call 'habitat.act' @{id=$item.id;action='eat'}),(Reply));Await {$world=@((Api '/api/capabilities').plugins|Where-Object id -eq 'habitat')[0].current;@($world.objects|Where-Object id -eq $item.id).Count-eq0} 'actual consumption finished' 35
 $threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-effects')[0];Check (@($thread.attempts|Where-Object {$_.action-eq'kitchen_result'-and$_.status-eq'consumed'-and$_.batch_id-eq$batch}).Count-eq1) 'only the actual consumed batch becomes a consumption attempt'
 Message @((Call 'body.observe' @{}),(Reply));Check ($null-eq(Feedback).result.personal_pursuit) 'consumed object no longer claims current occupation'
 Message @((Call 'activity.enqueue' $pursue),(Reply),(Call 'canteen.cook' @{recipe_id='tea'}),(Silent));Await {$s=Api '/api/activities/status';$null-eq$s.personal_pursuit.active-and@($s.recent|Where-Object {$_.kind-eq'pursue_intention'-and$_.effect_outcome-eq'acted'}).Count-eq2} 'second actual batch started'
 $k=@((Api '/api/capabilities').plugins|Where-Object id -eq 'canteen')[0].current.current;$job=$k.job;Check ($job.batch_id-ne$batch) 'new cooking owns a different batch'
 Message @((Call 'canteen.cancel' @{job_id=$job.id}),(Reply));$threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-effects')[0]
 Check (@($thread.attempts|Where-Object {$_.status-eq'cancelled'-and$_.batch_id-eq$job.batch_id}).Count-eq1) 'cancellation is linked without inventing a cooked dish'
 Check (@($thread.attempts|Where-Object {$_.status-eq'ready'-and$_.batch_id-eq$job.batch_id}).Count-eq0) 'another batch completion cannot satisfy this cancelled one'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "Intention effects passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
