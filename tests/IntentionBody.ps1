param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/intention-body-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
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
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.backgroundBudget=@{enabled=$false;autoEconomy=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxToolRounds=6
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Life fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$true;respectSystemMotion=$false;reducedMotion=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;autonomyPursuitCooldownSeconds=10;showBubble=$false;reactionCacheEnabled=$false};habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false;localGatherEnabled=$false};canteen=@{enabled=$true;automatic=$false};reading=@{enabled=$true;autonomousReading=$false}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 function Widget{@((Api '/api/capabilities').plugins|Where-Object id -eq 'widget')[0].current}
 function World{@((Api '/api/capabilities').plugins|Where-Object id -eq 'habitat')[0].current}
 function Bag{@((Api '/api/capabilities').plugins|Where-Object id -eq 'canteen')[0].current.current.inventory}
 Await {(World).inventory_available} 'inventory bridge'
 Message @((Call 'habitat.create' @{kind='berry';placement='nearby'}),(Call 'self.notice' @{id='body-time';expected_revision=0;kind='intention';topic='先捡一颗莓果再走一会儿';note='想看看窗边的小物。';stance='want';source_ids=@('${event_id}')}),(Reply))
 $initial=Widget;$target=if($initial.x-gt.5){.05}else{.95}
 $pursue=@{commands=@(@{kind='pursue_intention';domain='desktop';intention=@{id='body-time';revision=1}})}
 $responses=@(@{response=(Call 'activity.enqueue' $pursue)},@{response=(Reply)},@{response=(Call 'habitat.gather' @{ingredient_id='berry';count=1})},@{response=(Call 'widget.move' @{x=$target;y=$initial.y});delay_ms=3500},@{response=(Silent);delay_ms=5000})
 @{nonce=[guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 60|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[guid]::NewGuid().ToString('N');text='独立合成经历：我去忙一会儿。'}
 Await {$job=@((Api '/api/state').jobs|Where-Object id -eq 'chat')[0];$job.state-eq'succeeded'-and$job.run_token-gt1} 'foreground seed'
 Await {(Bag).used-eq1} 'real collection while own model still running' 7
 Check ($null-ne(Api '/api/activities/status').personal_pursuit.active-and(World).objects.Count-eq0) 'own thinking no longer freezes an actual pickup'
 $beforeMove=Widget;Await {$w=Widget;$w.moving-and[Math]::Abs($w.x-$beforeMove.x)-gt.01-and$null-ne(Api '/api/activities/status').personal_pursuit.active} 'direct own movement during the next pending model response' 5
 Check ((Api '/api/activities/status').queued-eq0) 'own movement is not stranded behind the running own-time job'
 Message @((Reply));Await {!(Widget).moving} 'foreground interruption stops own movement' 3
 $position=(Widget).x;Start-Sleep -Milliseconds 250;Check ([Math]::Abs((Widget).x-$position)-lt.002) 'new conversation retains body priority without late movement'
 Message @((Call 'habitat.gather' @{ingredient_id='tomato';count=1}),(Reply));$empty=Feedback
 Check ($empty.ok-and$empty.result.finished-and!$empty.result.completed-and$empty.result.collected-eq0-and$empty.result.status-eq'no_matching_resource') 'an empty field returns its actual terminal result immediately'
 Check ((World).last_gather.id-eq$empty.result.gather_id-and$null-eq(World).gather) 'empty collection leaves no fake pending action'
 Message @((Call 'habitat.gather' @{ingredient_id='tomato';count=1}),(Call 'self.close' @{id='body-time';expected_revision=1;outcome='completed';note='完成了采集';source_ids=@('${last_source_id}')}),(Reply));Check (!(Feedback).ok-and(Feedback).error.code-eq'personal_completion_requires_result') 'empty search cannot prove an intention was accomplished'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "Intention body passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
