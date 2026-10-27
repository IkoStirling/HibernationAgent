param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/embodied-activities-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=Join-Path $root tmp;$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null
$env:HIBERNATION_SESSION_KEY='embodied-activities-fixture';$env:HIBERNATION_BODY_KEY='synthetic-body-fixture';$desktop=$null;$service=$null;$provider=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=20){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if(& $Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer embodied-activities-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Widget{(Api '/api/capabilities').plugins.Where({$_.id-eq'widget'})[0].current}
function Plan($Items,[int]$Delay=0){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_;delay_ms=$Delay}})}|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Wait-For {!(Api '/api/chat').busy} 'turn'}
function Plugin($Name,$Arguments){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Wait-For {$script:receipt=Api '/api/plugin/result';$script:receipt.state-in@('succeeded','failed')} $Name;Check ($script:receipt.result.ok) $Name;return $script:receipt.result}
function Feedback{$capture=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 70;@(($capture.messages[-1].content|ConvertFrom-Json -Depth 60).tool_results)}
try{
 $provider=Start-Process (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Wait-For {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.profiles=@($cfg.profiles[0]);$cfg.frontend.startupAnimation=$false;$cfg.maxStartsPerHour=1000;$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000
 $cfg.review=@{enabled=$false};$cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';size=168;autonomousMode=$true;autonomyIdleSeconds=5;autonomyRefillSeconds=30;roam=$true;respectSystemMotion=$false;showBubble=$false}}
 $cfg.profiles+=@{id='actor';label='Body fixture';enabled=$true;protocol='chat-completions';origin=$fixture;basePath='/v1';model='fixture-model';credentialEnvironment='HIBERNATION_BODY_KEY';supportsJsonObject=$true;timeoutMs=10000;maxOutputTokens=2048}
 $cfg|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root config.json) -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$service=$desktop.Child
 Wait-For {if($service.HasExited){throw 'backend exited'};Test-Path (Join-Path $root ready.json)} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='actor'};Wait-For {(Widget).animation.ready} 'PNG body ready'
 $null=Plugin 'widget.animate' @{state='resting';seconds=60};Wait-For {(Widget).state-eq'resting'} 'resting before input'
 $initial=Widget;$frame=$desktop.CaptureClient($desktop.Find('HibernationDinosaurOverlay'));$span=$frame.Width-168;$distance=[Math]::Min(420,$span*.4);$target=[Math]::Clamp($initial.x+$distance/$span,.05,.95)
 $commands=@(@{kind='move_to';seconds=0;x=$target;y=$initial.y},@{kind='pose';seconds=2;state='blink'},@{kind='wait';seconds=1})
 Plan @((Calls @((Tool 'body' 'body.observe' @{}),(Tool 'queue' 'activity.enqueue' @{commands=$commands}))),(Silent)) 300
 Message '先换个位置，再眨眼和停留'
 $capture=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 70;$inputContext=$capture.messages[1].content|ConvertFrom-Json -Depth 60
 Check ($inputContext.body_at_input.widget.state-eq'resting'-and$inputContext.body.widget.state-in@('receiving','preparing','listening')) 'actor sees pose before input separately from reply preparation'
 Check (@($inputContext.body.widget.animation_catalog|Where-Object state -eq resting).Count-eq1-and!$inputContext.body.widget.PSObject.Properties['paint_count']-and!$inputContext.body.widget.animation.PSObject.Properties['image']) 'model receives available semantic actions without frame files or paint diagnostics'
 $feedback=Feedback;Check ($feedback[0].ok-and$feedback[1].ok-and$feedback[1].result.accepted-eq3) 'actor can observe body and enqueue movement pose and wait together'
 Wait-For {(Api '/api/activities/status').active.kind-eq'move_to'} 'move active';Start-Sleep -Milliseconds 250
 Check ((Api '/api/activities/status').queued-eq2-and(Widget).moving) 'following pose stays queued while actual movement runs'
 Wait-For {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'move_to'-and$_.status-eq'completed'}).Count-eq1} 'actual arrival' 25
 Check ([Math]::Abs((Widget).x-$target)-lt.002-and[math]::Abs(((Widget).x-$initial.x)*$span)-gt200) 'model path reaches its chosen destination beyond legacy roaming distance'
 Wait-For {(Api '/api/activities/status').active.kind-eq'pose'-and(Widget).state-eq'blink'} 'authored blink pose'
 Wait-For {@((Api '/api/activities/status').recent|Where-Object status -eq completed).Count-ge3} 'all commands complete'
 Check ((Api '/api/activities/status').queued-eq0-and$null-eq(Api '/api/activities/status').active) 'arrival and timed commands advance serially and consume each intention once'
 $position=Widget;$target2=[Math]::Max(.05,$position.x-.2)
 Plan @((Calls @((Tool 'direct' 'widget.move' @{x=$target2;y=$position.y}))),(Silent));Message '直接移动也要在回答之后执行'
 Check ((Feedback)[0].result.queued-eq1-and!(Feedback)[0].result.completed) 'model direct movement is coordinated through deferred activity queue'
 Wait-For {(Api '/api/activities/status').active.kind-eq'move_to'} 'second move';Plan @((Silent));Message '先停下来听我说'
 $stop=Widget;Start-Sleep -Milliseconds 300;Check ($null-eq(Api '/api/activities/status').active-and(Api '/api/activities/status').queued-eq0-and[math]::Abs((Widget).x-$stop.x)-lt.001) 'new input cancels movement and remaining actions immediately'
 $bad=@(@{kind='move_to';seconds=0;x=.5;y=.5},@{kind='pose';seconds=1;state='breathing'})
 Plan @((Calls @((Tool 'fatigue' 'activity.enqueue' @{commands=$bad}))),(Silent));Message '不能在不疲劳时强迫呼吸动作'
 Check (!(Feedback)[0].ok-and(Feedback)[0].error.code-eq'activity_not_fatigued'-and(Api '/api/activities/status').queued-eq0) 'unavailable physiological pose rejects the whole batch without partial movement'
 $near=Widget;$smallTarget=[Math]::Clamp($near.x+.04,.05,.95)
 Plan @(@{version=1;commands=@(@{kind='move_to';seconds=0;x=$smallTarget;y=$near.y},@{kind='pose';seconds=1;state='blink'},@{kind='wait';seconds=1})})
 Wait-For {@((Api '/api/activities/status').recent|Where-Object {$_.source-like'activity-generation*'-and$_.status-eq'completed'}).Count-ge3} 'automatic model batch executes movement and pose' 50
 $capture=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 70;$planning=$capture.messages[1].content|ConvertFrom-Json -Depth 60
 Check ($planning.available_commands-contains'move_to'-and$planning.available_commands-contains'pose'-and$planning.body.widget.animation_catalog.Count-gt0-and$planning.queue_slots-le6) 'background planner receives live body action catalog and actual queue capacity'
 Check ((Api '/api/activities/status').last_error-eq'') 'valid generated batch leaves no activity error'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'graceful shutdown'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Embodied activities passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
