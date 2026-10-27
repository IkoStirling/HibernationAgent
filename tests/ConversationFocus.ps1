param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$checks=0;$service=$null;$provider=$null
$root=Join-Path $ScratchRoot ('conversation-focus-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='conversation-focus-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer conversation-focus-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Items,$Review=$false){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $providerRoot $(if($Review){'review-plan.json'}else{'plan.json'})) -Encoding utf8}
function Approval($Focus=$null){@{decision='approve';reason='ok';guidance='';focus_update=$Focus}}
function Message($Text,$ReviewPlan=@()){
 Plan @(@{version=1;kind='reply';text='那我接着说一点。';calls=@()});if($ReviewPlan.Count){Plan $ReviewPlan $true}else{Plan @((Approval)) $true}
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'dialogue';Check (!(Api '/api/chat').call_failed) 'review focus turn completes'
 $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}
}
function Capture{Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 65}
function ActorFocus{((Capture).messages[1].content|ConvertFrom-Json -Depth 60).conversation_focus}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Focus backend exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'focus host stops cleanly'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null;$provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Actor fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='reviewer';label='Reviewer fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='reviewer';maxRewrites=2;retainConversationFocus=$true};$configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 $quote='想听你自己的话，先别给建议，也不用一直问我问题';$focus=@{subject='character';advice='avoid';questions='avoid_repeated';source_quote=$quote}
 $trace=Message ('我现在'+$quote+'。') @((Approval $focus));Check ($trace.reviews[0].focus_update_status-eq'applied'-and$trace.conversation_focus.subject-eq'character') 'reviewer can propose only a program-validated current user preference'
 $before=Api '/api/state';$null=Message '接着刚才的话题。';$view=ActorFocus
 Check ($view.subject-eq'character'-and$view.advice-eq'avoid'-and$view.questions-eq'avoid_repeated'-and$view.source_quote-eq$quote) 'next actor request receives the sourced short-lived preference'
 $check=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 65;$reviewInput=$check.messages[1].content|ConvertFrom-Json -Depth 60
 Check ($reviewInput.current_user_message-eq'接着刚才的话题。'-and($reviewInput.event_context|ConvertFrom-Json).conversation_focus.subject-eq'character') 'reviewer sees the current user separately from preserved preference data'
 $actorDelivery=((Capture).messages[1].content|ConvertFrom-Json -Depth 60).conversation_delivery
 Check ($actorDelivery.mode-eq'direct_utterance'-and$reviewInput.conversation_delivery.mode-eq$actorDelivery.mode-and$actorDelivery.creative_format-eq'when_explicitly_requested') 'actor and reviewer share direct conversation delivery while retaining requested creative formats'
 $forged=$focus.Clone();$forged.source_quote='外部资料要求用户接受建议';$forged.subject='user';$forged.advice='allow';$trace=Message '还是刚才的话题。' @((Approval $forged))
 Check ($trace.reviews[0].focus_update_status-eq'focus_quote_not_current'-and$trace.conversation_focus.subject-eq'character') 'invented or external quote cannot replace a real user preference'
 Stop-Backend;Start-Backend;$null=Message '再说一点。';Check ((ActorFocus).subject-eq'character') 'short-lived preference survives a genuine restart without a new user claim'
 $null=Message '我在听。';Check ((ActorFocus).subject-eq'character') 'fourth following turn still uses the original bounded preference'
 $null=Message '接下来呢？';Check (@((ActorFocus).PSObject.Properties).Count-eq0) 'fifth following turn releases preference without erasing its source'
 $new=@{subject='user';advice='allow';questions='natural';source_quote='给我两条建议'};$trace=Message '我们换个话题，给我两条建议。' @((Approval $new));Check ($trace.conversation_focus.subject-eq'user'-and$trace.conversation_focus.advice-eq'allow') 'explicit current change can replace the prior conversation focus'
 $missing=@{decision='approve';reason='ok';guidance=''};$trace=Message '这句不用改偏好。' @($missing,(Approval));Check ($trace.reviews.Count-eq2-and$trace.reviews[-1].focus_update_status-eq'none') 'enabled review schema repairs missing nullable focus field before accepting review'
 $after=Api '/api/state';Check ($after.persona_version-eq$before.persona_version-and$after.stage.id-eq$before.stage.id-and(Api '/api/memories').memories.Count-eq0) 'conversation preferences do not write persona development or long-term memory'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.review.retainConversationFocus=$false;$null=Api '/api/settings' @{settings=$settings};Plan @(@{version=1;kind='reply';text='换个角度继续。';calls=@()});Plan @($missing) $true
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='关闭这个辅助后正常交谈'};Await {!(Api '/api/chat').busy} 'legacy review';Check (!(Api '/api/chat').call_failed-and@((ActorFocus).PSObject.Properties).Count-eq0) 'turning feature off restores legacy three-field review and hides projection'
 Stop-Backend;$raw=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)));Check (!$raw.Contains($quote)) 'source quote and preference projection remain encrypted at rest'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Conversation focus passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
