param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/activity-reflection-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='activity-reflection-fixture';$service=$null;$provider=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer activity-reflection-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Call($Id,$Tool,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool=$Tool;args=$Arguments})}}
function Capture{Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 65}
function CapabilityContext{$text=(Capture).messages[0].content;$marker="`n可用能力：";$at=$text.IndexOf($marker);if($at-lt0){throw 'capability context missing'};$begin=$at+$marker.Length;$end=$text.IndexOf("`nSkill只提供",$begin);if($end-le$begin){throw 'capability context end missing'};$text.Substring($begin,$end-$begin)|ConvertFrom-Json -Depth 60}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
function Message($Items){Plan $Items;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='独立私下活动检查'};Await {!(Api '/api/chat').busy} 'message';Check (!(Api '/api/chat').call_failed) 'fixture dialogue completes'}
function Alone($Items){Plan $Items;$before=@((Api '/api/state').jobs|Where-Object id -eq solitude.once);$token=if($before.Count){$before[0].run_token}else{0};$null=Api '/api/solitude' @{};Await {$script:last=@((Api '/api/state').jobs|Where-Object id -eq solitude.once)[0];$script:last.state-eq'succeeded'-and$script:last.run_token-gt$token} 'solitude';$script:last.result|ConvertFrom-Json}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'reflection host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'reflection host stops cleanly';$desktop.Dispose()}
function Create-OwnDraft($Title){
 $command=@{kind='creative_note';seconds=0;purpose='留一小段还没有完成的故事';piece_kind='story';title=$Title;content='纸星只往边缘挪了一点，给下一阵风留出位置。'}
 Message @((Call 'draft' 'activity.enqueue' @{commands=@($command)}),(Silent));Await {@((Api '/api/activities/status').recent|Where-Object {$_.title-eq$Title-and$_.status-eq'completed'}).Count-eq1} 'actual own draft'
 @((Api '/api/activities/status').recent|Where-Object title -eq $Title)[0].source_id
}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.reflectOnActivities=$true;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.review=@{enabled=$false}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Reflection fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='reviewer';label='Review fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true});$cfg.plugins=@{widget=@{enabled=$true;roam=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;reactionCacheEnabled=$false;showBubble=$false};atelier=@{enabled=$true;maximumPieces=12}}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'};$original=Api '/api/state'
 $before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'empty private time does not call a model just to manufacture content'
 $source=Create-OwnDraft '先留一段';$notice=@{id='small-story';expected_revision=0;kind='interest';topic='短故事';stance='curious';note='REFLECTION_PRIVATE_MARKER 想试试留着尚未写满的结尾';source_ids=@($source)}
 $result=Alone @((Call 'interest' 'self.notice' $notice),(Silent));$capture=Capture;$context=$capture.messages[1].content|ConvertFrom-Json -Depth 55;$trace=Api '/api/trace' @{id=$result.trace_id}
 Check ($result.status-eq'completed'-and$context.task_context.activity_reflection-and@($context.authorized_context|Where-Object id -eq $source).Count-eq1-and$trace.tool_results[0].ok) 'actual own writing can support a private interest without an invented question'
 Check (@((Api '/api/chat').messages|Where-Object role -eq assistant).Count-eq0-and(Api '/api/memories').memories.Count-eq0) 'quiet reflection neither forces a visible diary nor creates a long-term memory'
 $before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'same completed work never causes repeated reflection requests'
 Message @((Silent));$self=((Capture).messages[1].content|ConvertFrom-Json -Depth 55).self
 Check ($self.threads.Count-eq1-and$self.threads[0].id-eq'small-story') 'a later conversation receives the independently formed interest'
 Stop-Backend;Start-Backend;$before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'considered source survives restart without replaying an opportunity'
 $next=Create-OwnDraft '第二小段';$settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.reflectOnActivities=$false;$null=Api '/api/settings' @{settings=$settings};$before=Requests;$result=Alone @((Silent))
 Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'operator can turn private activity consideration off without a restart'
 $settings.reflectOnActivities=$true;$null=Api '/api/settings' @{settings=$settings}
 Message @((Call 'source-memory' 'memory.remember' @{kind='inference';text='这段新创作只是计划继续的素材。';source_ids=@($next)}),(Silent));$feedback=((Capture).messages[-1].content|ConvertFrom-Json -Depth 55).tool_results[0];Check ($feedback.ok-and$feedback.result.id) 'derived memory has authentic new work evidence'
 $null=Api '/api/access' @{id=$feedback.result.id;accessible=$false};$before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'hidden underlying evidence cannot create a reflection opportunity'
 $null=Api '/api/access' @{id=$feedback.result.id;accessible=$true};$result=Alone @((Silent));Check ($result.status-eq'completed') 'restored evidence can be considered once while allowing silence'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.review=@{enabled=$true;profileId='reviewer';maxRewrites=1};$settings.contextControl=@{preset='full'};$null=Api '/api/settings' @{settings=$settings}
 $approval=@{decision='approve';reason='ok';guidance=''};$rewrite=@{decision='rewrite';reason='other';guidance='只修改对白，保留已接受的活动。'}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@($approval,$rewrite,$approval,$approval|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $root provider/review-plan.json) -Encoding utf8
 $repair=@{commands=@(@{kind='creative_note';seconds=0;purpose='只留一个开头';piece_kind='story';title='改写不重排';content='钥匙躲在壶嘴上，只露出一个齿尖。'})}
 Message @((Call 'before-repair' 'activity.enqueue' $repair),(Silent),(Call 'after-repair' 'activity.enqueue' $repair),(Silent));Await {$a=Api '/api/activities/status';$a.queued-eq0-and$null-eq$a.active} 'repaired effect settles'
 $done=@((Api '/api/activities/status').recent|Where-Object title -eq '改写不重排');$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$trace=Api '/api/trace' @{id=($job.result|ConvertFrom-Json).trace_id}
 Check ($done.Count-eq1-and$done[0].status-eq'completed'-and$trace.tool_results.Count-eq2-and$trace.tool_results[1].reused_after_review) 'reviewed rephrasing cannot queue or write the same draft twice'
 # Finish that genuine queued opportunity first, then distinguish administrator
 # fixtures from the model's own directly executed, silent creation.
 $null=Alone @((Silent))
 $null=Api '/api/plugin/test' @{tool='atelier.write';args=@{id='operator-fixture';expected_revision=0;kind='note';title='管理员试用';content='这不是角色自己写的。';stage='draft'}};Await {(Api '/api/plugin/result').state-in@('succeeded','failed')} 'administrator writing'
 Check ((Api '/api/plugin/result').result.ok) 'administrator fixture creates its separate draft'
 $before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'administrator tool test is not borrowed as the character private creation'
 Message @((Call 'direct-own-draft' 'atelier.write' @{id='direct-own-work';expected_revision=0;kind='note';title='直接留下的小事';content='DIRECT_PRIVATE_WORK_MARKER 今天想记住一个没画完的圆。';stage='draft'}),(Silent));$feedback=((Capture).messages[-1].content|ConvertFrom-Json -Depth 55).tool_results[0];$directSource=$feedback.source_id
 Check ($feedback.ok-and$directSource) 'character can directly create a real draft without speaking or queueing'
 $ambient=@((CapabilityContext).plugins|Where-Object id -eq atelier)[0].current;Check ($ambient.context_detail-eq'ambient_index'-and$ambient.details_tool-eq'atelier.list'-and!$ambient.PSObject.Properties['continuations']) 'ordinary dialogue knows the workshop without repeating its old prose'
 $result=Alone @((Silent));$context=(Capture).messages[1].content|ConvertFrom-Json -Depth 55
 Check ($result.status-eq'completed'-and$context.task_context.activity_reflection-and@($context.authorized_context|Where-Object id -eq $directSource).Count-eq1) 'direct silent character work becomes a sourced private consideration opportunity'
 $private=@((CapabilityContext).plugins|Where-Object id -eq atelier)[0].current;Check (@($private.continuations).Count-gt0-and($private|ConvertTo-Json -Depth 30).Contains('DIRECT_PRIVATE_WORK_MARKER')) 'private consideration retains actual draft details for independent continuation'
 $before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'direct creation is considered once rather than replayed on every quiet period'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 60|ConvertFrom-Json -AsHashtable;$settings.plugins.habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;maxObjects=4};$null=Api '/api/settings' @{settings=$settings}
 $null=Api '/api/plugin/test' @{tool='habitat.create';args=@{kind='seed'}};Await {(Api '/api/plugin/result').state-in@('succeeded','failed')} 'operator object fixture'
 $before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'an operator-created object does not impersonate the character own experience opportunity'
 Message @((Call 'own-object' 'habitat.create' @{kind='seed'}),(Silent));$source=((Capture).messages[-1].content|ConvertFrom-Json -Depth 60).tool_results[0].source_id
 $result=Alone @((Silent));$context=(Capture).messages[1].content|ConvertFrom-Json -Depth 60
 Check ($result.status-eq'completed'-and@($context.task_context.references|Where-Object id -eq $source).Count-eq1) 'a genuinely character-created desk object can be privately reconsidered'
 $before=Requests;$result=Alone @((Silent));Check ($result.status-eq'no_update'-and(Requests)-eq$before) 'a small object does not force repeated private summaries'
 $state=Api '/api/state';Check ($state.persona_version-eq$original.persona_version-and$state.stage.id-eq$original.stage.id) 'private consideration does not overwrite persona or stage'
 Message @((Call 'deliberate-workshop-look' 'capabilities.list' @{}),(Silent));$look=((Capture).messages[-1].content|ConvertFrom-Json -Depth 65).tool_results[0];$looked=@($look.result.plugins|Where-Object id -eq atelier)[0].current
 Check ($look.ok-and@($looked.continuations).Count-gt0-and($looked|ConvertTo-Json -Depth 30).Contains('DIRECT_PRIVATE_WORK_MARKER')) 'deliberate capability observation still retrieves full workshop contents'
 Stop-Backend;$raw=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)));Check (!$raw.Contains('REFLECTION_PRIVATE_MARKER')) 'private interest and consideration history remain encrypted'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Activity reflection passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
