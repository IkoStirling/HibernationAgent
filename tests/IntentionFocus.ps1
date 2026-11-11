param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/intention-focus-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
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
function Observe{Message @((Call 'self.observe' @{query='旧纸条上的故事'}),(Reply));(Feedback).result.threads}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'fixture exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown';$desktop.Dispose();$script:desktop=$null}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.backgroundBudget=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxToolRounds=6
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Life fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;autonomyPursuitCooldownSeconds=10;showBubble=$false;reactionCacheEnabled=$false};habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false;localGatherEnabled=$false};canteen=@{enabled=$true;automatic=$false};reading=@{enabled=$true;autonomousReading=$false};atelier=@{enabled=$true}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 $old=@{id='old-story';expected_revision=0;kind='intention';topic='旧纸条上的故事';note='FOCUS_MARKER 想接续那截多余的尾巴';stance='want';source_ids=@('${event_id}')}
 Message @((Call 'memory.remember' @{kind='inference';text='FOCUS_ORIGIN_CONTROL 独立合成的最早依据';source_ids=@('${event_id}')}),(Call 'self.notice' $old),(Reply))
 $focusMemory=@((Api '/api/memories').memories|Where-Object {$_.text.Contains('FOCUS_ORIGIN_CONTROL')})[0].id;Check ([bool]$focusMemory) 'old origin control exists'
 for($i=1;$i-lt10;$i++){$old.expected_revision=$i;$old.note='FOCUS_MARKER 第'+$i+'次想起那截尾巴';Message @((Call 'self.notice' $old),(Reply))}
 for($i=0;$i-lt23;$i++){Message @((Call 'self.notice' @{id=('fresh-'+$i);expected_revision=0;kind='interest';topic=('新来的细节'+$i);note='合成场景中的另一个小细节';stance='curious';source_ids=@('${event_id}')}),(Reply));Check ((Feedback).ok) 'each newer interest is actually saved'}
 Message @((Call 'self.notice' @{id='fresh-overflow';expected_revision=0;kind='interest';topic='超出活跃关注上限的细节';note='合成边界检查';stance='curious';source_ids=@('${event_id}')}),(Reply));Check (!(Feedback).ok-and(Feedback).error.code-eq'personal_active_limit') 'the 25th active thread is explicitly rejected, never silently assumed saved'
 $pursue=@{commands=@(@{kind='pursue_intention';domain='creative';intention=@{id='old-story';revision=10}})}
 $null=Api '/api/access' @{id=$focusMemory;accessible=$false};Message @((Call 'activity.enqueue' $pursue),(Reply));Check (!(Feedback).ok-and(Feedback).error.code-eq'activity_intention_changed_or_unavailable') 'an old unpinned origin still governs execution rights'
 $null=Api '/api/access' @{id=$focusMemory;accessible=$true}
 $piece=@{id='old-story-piece';expected_revision=0;title='纸条上的尾巴';kind='story';content='那截尾巴先从纸边探出了一点。';stage='draft'}
 Message @((Call 'activity.enqueue' $pursue),(Reply),(Call 'atelier.write' $piece),(Silent))
 Await {$s=Api '/api/activities/status';$null-eq$s.personal_pursuit.active-and@($s.recent|Where-Object {$_.kind-eq'pursue_intention'-and$_.effect_outcome-eq'acted'}).Count-eq1} 'old intention actually continued'
 $job=@((Api '/api/state').jobs|Where-Object id -eq 'autonomy.personal-pursuit')[0];$trace=Api '/api/trace' @{id=('trace-autonomy.personal-pursuit-'+$job.run_token)}
 Check ($trace.personal_context.threads[0].id-eq'old-story'-and$trace.personal_context.threads[0].note.Contains('FOCUS_MARKER')) 'selected old intention receives its complete meaning despite many newer interests'
 Check ($trace.personal_context.threads.Count-le8-and$trace.task_context.references.Count-le4) 'personal context and pinned raw origins remain bounded'
 Check (@($trace.tool_results|Where-Object {$_.tool-eq'atelier.write'-and$_.ok}).Count-eq1) 'bounded context still allows actual creation'
 $threads=Observe;$thread=@($threads|Where-Object id -eq 'old-story')[0];Check ($thread.revision-eq10-and$thread.status-eq'active') 'attention and progress do not rewrite or auto-complete the old intention'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "Intention focus passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
