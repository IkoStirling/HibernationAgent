param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/intention-life-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
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
 $note=@{id='tea-life';expected_revision=0;kind='intention';topic='泡一杯茶';note='想让杯子热起来，然后再决定要不要喝。';stance='want';source_ids=@('${event_id}')}
 Message @((Call 'self.notice' $note),(Reply));Check ((Feedback).ok-and(Feedback).result.revision-eq1) 'intention really saved from a conversation source'
 Check (@((Api '/api/activities/status').personal_pursuit.domains|Where-Object {$_-eq'reading'}).Count-eq0) 'installed reading plugin alone does not grant autonomous network reading'
 $noReading=@{commands=@(@{kind='pursue_intention';domain='reading';intention=@{id='tea-life';revision=1}})};Message @((Call 'activity.enqueue' $noReading),(Reply));Check (!(Feedback).ok-and(Feedback).error.code-eq'personal_pursuit_domain_unavailable') 'autonomous reading opt-in is enforced before queue admission'
 $pursue=@{commands=@(@{kind='pursue_intention';domain='kitchen';intention=@{id='tea-life';revision=1}})}
 $pause=@{id='tea-life';expected_revision=1;choice='pause';when='soon';note='茶正煮着，先留它一会儿。';source_ids=@('${last_source_id}')}
 Message @((Call 'activity.enqueue' $pursue),(Reply),(Call 'canteen.plan' @{recipe_id='tea'}),(Call 'computer.open_application' @{id='notepad'}),(Call 'canteen.cook' @{recipe_id='tea'}),(Call 'self.reconsider' $pause),(Silent))
 Await {$s=Api '/api/activities/status';$null-eq$s.personal_pursuit.active-and@($s.recent|Where-Object {$_.kind-eq'pursue_intention'-and$_.effect_outcome-eq'acted'}).Count-eq1} 'actual tool step retires'
 $job=@((Api '/api/state').jobs|Where-Object id -eq 'autonomy.personal-pursuit')[0];$trace=Api '/api/trace' @{id=('trace-autonomy.personal-pursuit-'+$job.run_token)};$trace|ConvertTo-Json -Depth 70|Set-Content (Join-Path $root private-step-trace.json) -Encoding utf8
 Check (@($trace.tool_results|Where-Object {$_.tool-eq'canteen.cook'-and$_.ok}).Count-eq1) 'own intention starts actual shared kitchen job'
 Check (@($trace.tool_results|Where-Object {$_.tool-eq'computer.open_application'-and$_.error_code-eq'personal_pursuit_domain_denied'}).Count-eq1) 'kitchen time cannot launch an external application'
 $threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-life')[0];Check ($thread.status-eq'active'-and$thread.availability-eq'paused'-and$thread.revision-eq2) 'pause is reversible and does not falsely complete the goal'
 Check (@($thread.attempts|Where-Object {$_.action-eq'pursue_intention'-and$_.result_sources.Count-gt0}).Count-eq1) 'actual tool sources retained in existing personal progress'
 Message @((Call 'activity.enqueue' $pursue),(Reply));Check (!(Feedback).ok) 'old intention binding cannot execute after reconsideration'
 $until=$thread.intention_pause.until_ms;Stop-Backend;Start-Backend;$threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-life')[0]
 Check ($thread.intention_pause.until_ms-eq$until-and$thread.availability-eq'paused') 'pause horizon survives encrypted restart'
 $resume=@{id='tea-life';expected_revision=2;choice='resume';note='想回来看看那杯茶。';source_ids=@('${event_id}')};Message @((Call 'self.reconsider' $resume),(Reply));Check ((Feedback).ok-and(Feedback).result.revision-eq3) 'same intention can be picked up with a new semantic version'
 $threads=Observe;$thread=@($threads|Where-Object id -eq 'tea-life')[0];Check ($thread.availability-eq'available') 'resuming restores availability without replaying old commands'
 Message @((Call 'self.reconsider' $resume),(Reply));Check (!(Feedback).ok-and(Feedback).error.code-eq'personal_revision_conflict') 'stale reconsideration is rejected'
 $decision=@{id='tea-life';expected_revision=3;choice='pause';when='soon';note='这一会儿先留给别的事。';source_ids=@('${event_id}')}
 Message @((Call 'memory.remember' @{kind='inference';text='独立合成经历：这一会儿先留给别的事，检查暂停决定的来源可见性。';source_ids=@('${event_id}')}),(Call 'self.reconsider' $decision),(Reply));Check ((Feedback).ok-and(Feedback).result.revision-eq4) 'new decision retains its own source'
 $memory=@((Api '/api/memories').memories|Where-Object {$_.text.Contains('检查暂停决定的来源可见性')})[0];Check ([bool]$memory.id) 'owner visibility fixture stored'
 $resume.expected_revision=4;Message @((Call 'self.reconsider' $resume),(Reply));Check ((Feedback).ok-and(Feedback).result.revision-eq5) 'decision history survives another resumption'
 $null=Api '/api/access' @{id=$memory.id;accessible=$false};$threads=Observe;Check (@($threads|Where-Object id -eq 'tea-life').Count-eq0) 'hidden decision source removes intention from model context'
 $pursue.commands[0].intention.revision=5;Message @((Call 'activity.enqueue' $pursue),(Reply));Check (!(Feedback).ok) 'hidden decision source also blocks execution with the current version'
 $null=Api '/api/access' @{id=$memory.id;accessible=$true};$threads=Observe;Check (@($threads|Where-Object {$_.id-eq'tea-life'-and$_.revision-eq5-and$_.availability-eq'available'}).Count-eq1) 'restored source exposes the same intention without inventing history'
 Message @((Call 'self.close' @{id='tea-life';expected_revision=5;outcome='abandoned';note='现在想换一件事。';source_ids=@('${event_id}')}),(Reply));Check ((Feedback).ok) 'changing mind may close an unfinished intention'
 Message @((Call 'self.observe' @{status='closed';query='泡一杯茶'}),(Reply));Check ((Feedback).result.threads[0].availability-eq'closed') 'closed intention is never mislabeled paused'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "Intention life passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
