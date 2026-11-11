param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/intention-deadline-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
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
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;autonomyPursuitCooldownSeconds=3600;autonomyPursuitMaxSeconds=5;showBubble=$false;reactionCacheEnabled=$false};habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false;localGatherEnabled=$false};canteen=@{enabled=$true;automatic=$false};reading=@{enabled=$true;autonomousReading=$false}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 $note=@{id='tea-deadline';expected_revision=0;kind='intention';topic='给自己泡一杯茶';note='让茶先煮着。';stance='want';source_ids=@('${event_id}')};Message @((Call 'self.notice' $note),(Reply))
 $pursue=@{commands=@(@{kind='pursue_intention';domain='kitchen';intention=@{id='tea-deadline';revision=1}})}
 $late=Call 'self.notice' @{id='late-after-window';expected_revision=0;kind='interest';topic='过期响应不该写入';note='LATE_WINDOW_MARKER';stance='curious';source_ids=@('${last_source_id}')}
 $responses=@(@{response=(Call 'activity.enqueue' $pursue)},@{response=(Reply)},@{response=(Call 'canteen.cook' @{recipe_id='tea'})},@{response=$late;delay_ms=8000})
 @{nonce=[guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 60|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[guid]::NewGuid().ToString('N');text='独立合成经历：去忙一件自己的小事。'};Await {$job=@((Api '/api/state').jobs|Where-Object id -eq 'chat')[0];$job.state-eq'succeeded'-and$job.run_token-gt1} 'initial foreground turn'
 Await {$s=Api '/api/activities/status';$null-ne$s.personal_pursuit.active} 'private window admitted';$clock=[Diagnostics.Stopwatch]::StartNew()
 Await {$s=Api '/api/activities/status';$null-eq$s.personal_pursuit.active-and@($s.recent|Where-Object {$_.kind-eq'pursue_intention'-and$_.effect_outcome-eq'interrupted'}).Count-eq1} 'window deadline interrupts the slow provider response' 12
 Check ($clock.Elapsed.TotalSeconds-lt7) 'five-second own window does not wait for an eight-second late model'
 $threads=Observe;Check (@($threads|Where-Object id -eq 'late-after-window').Count-eq0) 'late proposal cannot write memory after the window ends'
 $thread=@($threads|Where-Object id -eq 'tea-deadline')[0];Check (@($thread.attempts|Where-Object {$_.action-eq'pursue_intention'-and$_.status-eq'interrupted'-and$_.result_sources.Count-gt0}).Count-eq1) 'interruption still retains the actual accepted cooking result'
 Message @((Call 'body.observe' @{}),(Reply));Check ((Feedback).result.personal_pursuit.phase-eq'waiting_for_result') 'a real cooking job continues after the thought window closes'
 Check ((Api '/api/chat').can_send-and!(Api '/api/chat').call_failed) 'normal conversation remains available after own time ends'
 Check ((Api '/api/activities/status').personal_pursuit.wait_reason-eq'cooldown') 'the saved admission interval explains why another window is not ready'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 60|ConvertFrom-Json -AsHashtable;$settings.plugins.widget.autonomyPursuitEnabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Check ((Api '/api/activities/status').personal_pursuit.enabled-eq$false) 'own-intention continuation has an independent off switch'
 Message @((Call 'activity.enqueue' $pursue),(Reply));Check (!(Feedback).ok) 'off switch removes the continuation entry before execution'
 Message @((Call 'activity.enqueue' @{commands=@(@{kind='wait';seconds=1})}),(Reply));Check ((Feedback).ok) 'finite simple activities remain available when full continuation is off'
 $settings.plugins.widget.autonomyPursuitEnabled=$true;$null=Api '/api/settings' @{settings=$settings};$null=Api '/api/control' @{paused=$true}
 Check ((Api '/api/activities/status').personal_pursuit.wait_reason-eq'paused') 'manual pause takes precedence over the cooldown reason'
 $null=Api '/api/control' @{paused=$false}
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "Intention deadline passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
