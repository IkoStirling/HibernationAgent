param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/autonomous-carry-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='autonomous-carry-fixture';$service=$null;$provider=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer autonomous-carry-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Plan($Items,[int]$Delay=0,[switch]$Review,[int]$HttpStatus=200){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_;delay_ms=$Delay;http_status=$HttpStatus}})}|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $root provider/$file) -Encoding utf8}
function Capture{Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 70}
function Requests($Review=$false){$file=if($Review){'review-requests.jsonl'}else{'requests.jsonl'};$path=Join-Path $root provider/$file;if(Test-Path -LiteralPath $path){return @(Get-Content -LiteralPath $path).Count};0}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Call($Id,$Tool,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool=$Tool;args=$Arguments})}}
function Message($Items){Plan $Items;Plan @($script:approval) -Review;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='合成相处：刚才那段未写完的故事可以先留在这里。'};Await {!(Api '/api/chat').busy} 'fixture dialogue';Check (!(Api '/api/chat').call_failed) 'fixture dialogue succeeds'}
function Auto($Enabled){$settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$settings.plugins.widget.autonomousMode=$Enabled;$null=Api '/api/settings' @{settings=$settings};Await {(Api '/api/activities/status').enabled-eq$Enabled} 'autonomy setting';if(!$Enabled){Await {!(Api '/api/activities/status').generating} 'planner stopped'}}
function Note($Id){@{id=$Id;expected_revision=0;kind='intention';topic='未写完的小故事';note='AUTONOMOUS_PRIVATE_MARKER 想在下次有空时续上那个没画完的圆';stance='want';source_ids=@($script:workSource)}}
function Batch($Note,$During=$null,$Verdict=$null,[string]$ExpectedError='',[int]$ReviewHttpStatus=200){
 Auto $false;$before=(Api '/api/activities/status').personal_carry_feedback.generation;$calls=Requests;$reviews=Requests $true
 Plan @(@{version=1;commands=@(@{kind='wait';seconds=1});carry_forward=@($Note)}) $(if($During){1800}else{0})
 if($Verdict-and!$ExpectedError){$ExpectedError='activity_review_rejected'}
 Plan @($(if($Verdict){$Verdict}else{$script:approval})) -Review -HttpStatus $ReviewHttpStatus;Auto $true
 Await {(Requests)-gt$calls} 'background request';$context=(Capture).messages[1].content|ConvertFrom-Json -Depth 65
 if($During){&$During}
 Await {$s=Api '/api/activities/status';!$s.generating-and$(if($ExpectedError){$s.last_error-eq$ExpectedError}else{$s.personal_carry_feedback.generation-and$s.personal_carry_feedback.generation-ne$before})} 'background carry outcome'
 $status=Api '/api/activities/status';Auto $false
 return @{context=$context;feedback=$status.personal_carry_feedback;error=$status.last_error;actor_calls=(Requests)-$calls;review_calls=(Requests $true)-$reviews}
}
function Observe{$null=Message @((Call self self.observe @{}),(Silent));((Capture).messages[-1].content|ConvertFrom-Json -Depth 65).tool_results[0].result}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'autonomous carry host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'host exits cleanly';$desktop.Dispose();$script:desktop=$null}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false
 $cfg.plugins=@{widget=@{enabled=$true;roam=$false;autonomousMode=$false;autonomyIdleSeconds=5;autonomyRefillSeconds=300;showBubble=$false;reactionCacheEnabled=$false};atelier=@{enabled=$true}}
 $cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Actor fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='reviewer';label='Reviewer fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true});$cfg.review=@{enabled=$true;profileId='reviewer';maxRewrites=1}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content -LiteralPath $configPath -Encoding utf8;$approval=@{decision='approve';reason='ok';guidance=''};Start-Backend;$null=Api '/api/model' @{id='actor'};$original=Api '/api/state'
 Message @((Call work atelier.write @{id='own-unfinished';expected_revision=0;kind='story';title='未画完的圆';content='一个圆缺了一小截，纸上的风从那里穿过。';stage='draft'}),(Silent));$workSource=((Capture).messages[-1].content|ConvertFrom-Json -Depth 60).tool_results[0].source_id;Check ([bool]$workSource) 'real private work provides an original source'
 Message @((Call source memory.remember @{kind='inference';text='那段创作还可以继续，只是一个打算。';source_ids=@($workSource)}),(Silent));$memory=((Capture).messages[-1].content|ConvertFrom-Json -Depth 60).tool_results[0].result.id
 $note=Note own-continuation;$first=Batch $note
 Check ($first.feedback.results[0].ok-and$first.feedback.results[0].revision-eq1) 'background actor can save its own sourced intention'
 Check ($first.actor_calls-eq1-and$first.review_calls-eq1) 'private carry uses the existing actor and review calls without another model request'
 Check (@($first.context.evidence|Where-Object {$_.id-eq$workSource-and$_.kind-eq'tool_observation'-and$_.data.result.written}).Count-eq1) 'planner receives a bounded real writing receipt rather than invented evidence'
 $repeat=$note.Clone();$repeat.expected_revision=1;$again=Batch $repeat
 Check ($again.feedback.results[0].ok-and$again.feedback.results[0].deduplicated-and$again.feedback.results[0].revision-eq1) 'same source and meaning do not strengthen or version the intention again'
 Check ($again.context.previous_carry_forward_results.generation-eq$first.feedback.generation) 'next planning request receives the actual prior private-operation outcome'
 $old=Batch $note;Check (!$old.feedback.results[0].ok-and$old.feedback.results[0].error.code-eq'personal_revision_conflict') 'stale personal versions remain rejected during background planning'
 $forged=Note forged;$forged.source_ids=@('activity-generation-invented');$bad=Batch $forged;Check (!$bad.feedback.results[0].ok-and$bad.feedback.results[0].error.code-eq'personal_source_not_available') 'a plan cannot serve as its own factual source'
 $hidden=Batch (Note hidden-after-start) {$null=Api '/api/access' @{id=$memory;accessible=$false}}
 Check (@($hidden.context.evidence|Where-Object id -eq $workSource).Count-eq1-and!$hidden.feedback.results[0].ok-and$hidden.feedback.results[0].error.code-eq'personal_source_not_available') 'source access is rechecked after the external actor and reviewer finish'
 $null=Api '/api/access' @{id=$memory;accessible=$true}
 $denied=Batch (Note reviewer-denied) $null @{decision='deny';reason='continuity';guidance='不保存这个候选。'}
 Check ($denied.error-eq'activity_review_rejected'-and$denied.feedback.generation-eq$hidden.feedback.generation) 'rejected background candidates never execute their private carry'
 $invalid=Batch (Note invalid-review) $null 'not review JSON' 'activity_review_format'
 Check ($invalid.error-eq'activity_review_format'-and$invalid.feedback.generation-eq$hidden.feedback.generation) 'malformed review is not reported as a semantic rejection and never authorizes carry'
 $unavailable=Batch (Note unavailable-review) $null $approval 'activity_review_unavailable' 503
 Check ($unavailable.error-eq'activity_review_unavailable'-and$unavailable.feedback.generation-eq$hidden.feedback.generation) 'reviewer service failure remains distinct from rejecting a character choice'
 Auto $false;$before=Requests;Plan @(@{version=1;commands=@();carry_forward=@((Note interrupted-plan))}) 1800;Plan @($approval) -Review;Auto $true;Await {(Requests)-gt$before} 'delayed planning starts'
 Plan @((Silent));$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='先停一下，我来了。'};Await {!(Api '/api/chat').busy-and!(Api '/api/activities/status').generating} 'new input cancels the old plan';Auto $false
 $self=Observe;Check ($self.threads.Count-eq1-and$self.threads[0].id-eq'own-continuation'-and$self.threads[0].status-eq'active') 'only approved actual notes survive; completing a short wait does not complete the larger intention'
 $state=Api '/api/state';Check ($state.persona_version-eq$original.persona_version-and$state.stage.id-eq$original.stage.id) 'background carry preserves core persona and development stage'
 Stop-Backend;Start-Backend;$self=Observe;Check ($self.threads.Count-eq1-and$self.threads[0].id-eq'own-continuation') 'own intention persists across a real restart'
 Stop-Backend;$bytes=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)));Check (!$bytes.Contains('AUTONOMOUS_PRIVATE_MARKER')) 'background private notes and feedback remain encrypted at rest'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Autonomous carry passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
