param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('cache-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='cache-pipeline-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$until=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$until){if(&$Probe){return};Start-Sleep -Milliseconds 45};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer cache-pipeline-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 75 -Compress))};Invoke-RestMethod @p}
function Native($Id,$Name,$Arguments){@{fixture_native_calls=@(@{id=$Id;name=$Name;arguments=$Arguments})}}
function Plan($Items,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$out=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$out.trace_id}}
function Start-Backend{$script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'Cache fixture exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown'}
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.maxToolRounds=3;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Cache actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsToolCalls=$true;supportsStreaming=$true;supportsJsonObject=$true},@{id='review';label='Cache review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1;retainConversationFocus=$true};$cfg.contextControl=@{enabled=$true;preset='balanced';reviewAttempts=2;reviewLayout='snapshots'};$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $root config.json) -Encoding utf8

 $first='先别给建议，也不用一直问我问题';$last='回答之后可以自然问我一个问题'
 $focus1=@{subject='character';advice='avoid';questions='avoid_repeated';source_quote=$first}
 $focus2=@{subject='character';advice='avoid';questions='natural';source_quote=$last}
 Plan @(@{unexpected='invalid-review-fixture'},@{decision='approve';reason='ok';guidance='';focus_update=$focus1},@{decision='approve';reason='ok';guidance='';focus_update=$focus2}) $true
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 Plan @((Native 'remember' 'ha_memory_DOT_remember' @{kind='conversation';text='合成纸船';source_ids=@('${event_id}')}),(Native 'state' 'ha_state_DOT_observe' @{}),(Native 'final' 'ha_turn_reply' @{text='纸船先留在这里。'}))
 $trace=Message ('先记住这只合成纸船。'+$first+'。'+$last+'。')
 Check (!(Api '/api/chat').call_failed) 'approved tool and reply pipeline completes'
 $wire=@(Get-Content (Join-Path $providerRoot requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json -Depth 90}|Where-Object model -eq 'fixture-model')
 Check ($wire.Count-eq3) 'one memory operation, one read and one final actor call'
 Check (($wire[0].messages|ConvertTo-Json -Depth 85 -Compress)-ceq($wire[1].messages[0..($wire[0].messages.Count-1)]|ConvertTo-Json -Depth 85 -Compress)) 'focus approval never rewrites an already sent event'
 Check (($wire[1].messages|ConvertTo-Json -Depth 85 -Compress)-ceq($wire[2].messages[0..($wire[1].messages.Count-1)]|ConvertTo-Json -Depth 85 -Compress)) 'native call, receipt and pending updates all remain an unchanged prefix'
 $updates=@($wire[1].messages|ForEach-Object {try{($_.content|ConvertFrom-Json -Depth 60).runtime_event}catch{}}|Where-Object {$_})
 Check ($updates.Count-eq1-and$updates[0].kind-eq'focus.changed'-and$updates[0].changes.conversation_focus.questions-eq'avoid_repeated') 'current preference arrives as a separate ordered runtime event'
 $current=Model-CurrentEvent $wire[2];Check ($current.conversation_focus.questions-eq'avoid_repeated') 'event materialization produces the latest actor-visible state'
 $reviews=@(Get-Content (Join-Path $providerRoot review-requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json -Depth 90}|Where-Object model -eq 'fixture-reviewer')
 Check (($reviews[0].messages|ConvertTo-Json -Depth 85 -Compress)-ceq($reviews[1].messages[0..($reviews[0].messages.Count-1)]|ConvertTo-Json -Depth 85 -Compress)) 'review snapshots append without altering any already sent evidence or state'
 Check ($reviews.Count-eq3-and($reviews[1].messages|ConvertTo-Json -Depth 85 -Compress)-ceq($reviews[2].messages[0..($reviews[1].messages.Count-1)]|ConvertTo-Json -Depth 85 -Compress)) 'a format-repair input remains in the prefix of the later approved review'
 Check (@($reviews[1].messages|Where-Object role -in @('assistant','tool')).Count-eq0) 'review context contains no previous reviewer verdict and no executable tool transcript'
 $review=Get-Content (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 85;$context=(Model-ReviewEvent $review).event_context|ConvertFrom-Json -Depth 80
 Check ($context.conversation_focus.questions-eq'avoid_repeated'-and!$context.projection) 'reviewer receives a full current snapshot instead of a stale initial message'
 Check ($trace.runtime_events.queued-eq2-and$trace.runtime_events.after_reply-eq1) 'late approved preference is recorded as a post-reply event'
 Plan @(@{decision='approve';reason='ok';guidance='';focus_update=$null}) $true
 Plan @((Native 'next' 'ha_turn_reply' @{text='继续。'}));$next=Message '接着刚才说。'
 $request=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90
 Check ($next.session_projection.mode-eq'append') 'runtime events remain compatible with encrypted cross-turn projection'
 Check ((Model-CurrentEvent $request).conversation_focus.questions-eq'natural') 'post-reply preference reaches the next actor without retrospective editing'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output "Context events passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
