param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0;$root=Join-Path $ScratchRoot ('layered-review-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='layered-review-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$until=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$until){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer layered-review-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 75 -Compress))};Invoke-RestMethod @p}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items);presentation=@{speech=$false;bubble=$false};carry_forward=$null;appraisal=$null}}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@();presentation=@{speech=$false;bubble=$false};carry_forward=$null;appraisal=$null}}
function Verdict($Decision='approve'){@{decision=$Decision;reason=if($Decision-eq'approve'){'ok'}else{'tool_intent'};guidance=if($Decision-eq'approve'){''}else{'停止本次提案。'}}}
function Plan($Items,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {if($_-is[hashtable]-and$_.ContainsKey("fixture_status")){@{response="";http_status=$_.fixture_status}}else{@{response=$_}}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat/status').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$out=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$out.trace_id}}
function ReviewWire{@(Get-Content (Join-Path $providerRoot review-requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json -Depth 90})}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null;$provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.maxToolRounds=5;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;supportsStreaming=$false},@{id='review';label='Review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true;maxOutputTokens=8192;timeoutMs=10000;reasoningEffort='low'})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1;retainConversationFocus=$true};$cfg.contextControl=@{reviewPolicy='layered';reviewLayout='events';reviewReasoningEffort='none';reviewInitialTokens=1024;reviewAttempts=2};$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $root config.json) -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'fixture exited'};Test-Path (Join-Path $root ready.json)} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 $final=Verdict;$final.focus_update=$null
 Plan @($final) $true;Plan @((Calls @((Tool 'write' 'workspace.write' @{path='local.md';content='bounded local draft'}))),(Calls @((Tool 'read' 'workspace.read' @{mount='drafts';path='local.md';length=100}))),(Reply '草稿已保存。'));$trace=Message '保存草稿并重读'
 Check (!(Api '/api/chat/status').call_failed-and$trace.reviews.Count-eq1-and$trace.turns.Count-eq3) 'two local batches use program gates and one final review'
 Check ($trace.reviews_skipped.Count-eq2-and$trace.reviews[0].focus_requested-and$trace.reviews[0].target-eq'reply') 'focus extraction occurs only on final dialogue'
 Check ($trace.call_cost_breakdown.actor.calls-eq3-and$trace.call_cost_breakdown.reply_review.calls-eq1-and$trace.call_cost_breakdown.tool_review.calls-eq0) 'trace separates actor and semantic review costs without estimates'
 Check ((Get-Content (Join-Path $cfg.dataRoot drafts/local.md) -Raw)-eq'bounded local draft') 'bounded draft actually written'
 Plan @($final) $true;Plan @((Calls @((Tool 'escape' 'workspace.write' @{path='../escape.md';content='must not escape'}))),(Reply '这个位置写不了。'));$denied=Message '验证路径边界'
 Check (!(Test-Path (Join-Path $cfg.dataRoot escape.md))-and!$denied.tool_results[0].ok) 'skipping semantic review never bypasses workspace permissions'
 Plan @((Verdict 'deny')) $true;Plan @((Calls @((Tool 'local-mixed' 'workspace.write' @{path='mixed.md';content='must not write'}),(Tool 'memory-mixed' 'memory.remember' @{kind='conversation';text='mixed proposal';source_ids=@('${event_id}')}))));$blocked=Message '验证混合提案'
 Check ((Api '/api/chat/status').call_failed-and!(Test-Path (Join-Path $cfg.dataRoot drafts/mixed.md))) 'mixed sensitive batch denied before any local side effect'
 Check ($blocked.reviews.Count-eq1-and!$blocked.reviews[0].focus_requested-and!$blocked.reviews[0].continuity_requested) 'tool reviewer does not perform dialogue extraction'
 Plan @((Verdict),(Verdict),$final) $true;$start=(ReviewWire).Count
 Plan @((Calls @((Tool 'm1' 'memory.remember' @{kind='conversation';text='source bounded one';source_ids=@('${event_id}')}))),(Calls @((Tool 'm2' 'memory.remember' @{kind='conversation';text='source bounded two';source_ids=@('${event_id}')}))),(Reply '这两点留下了。'));$sensitive=Message '保留两点'
 Check (!(Api '/api/chat/status').call_failed-and$sensitive.reviews.Count-eq3) 'memory mutations retain independent intent reviews'
 $wire=ReviewWire;$first=$wire[$start];$second=$wire[$start+1];$prefix=@($second.messages|Select-Object -First $first.messages.Count)
 Check (($prefix|ConvertTo-Json -Depth 90 -Compress)-ceq($first.messages|ConvertTo-Json -Depth 90 -Compress)) 'later review preserves every previously sent message byte-equivalently'
 Check ($second.messages[-1].content.Contains('review_candidate')-and@($second.messages|Where-Object {$_.content.Contains('"review_event"')}).Count-gt0) 'new receipts and authority changes append as review events'
 $start=(ReviewWire).Count;Plan @(@{fixture_incomplete=$true},$final) $true;Plan @((Reply '重审同一份回复。'));$repaired=Message '验证审核截断重试'
 Check (!(Api '/api/chat/status').call_failed-and$repaired.turns.Count-eq1-and$repaired.reviews.Count-eq2) 'incomplete reviewer retries same candidate without regenerating actor'
 Check ($repaired.reviews[0].max_output_tokens-eq1024-and$repaired.reviews[1].max_output_tokens-eq8192-and$repaired.reviews[1].reasoning_effort-eq'none') 'review retry uses bounded escalation and configured non-thinking mode'
 $wire=ReviewWire;$first=$wire[$start];$retry=$wire[$start+1];$prefix=@($retry.messages|Select-Object -First $first.messages.Count)
 Check (($prefix|ConvertTo-Json -Depth 90 -Compress)-ceq($first.messages|ConvertTo-Json -Depth 90 -Compress)-and$retry.messages[-1].content.Contains('review_retry')) 'retry only appends notice and never includes incomplete output'
 Plan @(@{fixture_incomplete=$true},@{fixture_incomplete=$true}) $true;Plan @((Reply 'THIS_MUST_NOT_PUBLISH'));$exhausted=Message '验证审核失败关闭'
 Check ((Api '/api/chat/status').call_failed-and$exhausted.reviews.Count-eq2-and$exhausted.turns.Count-eq1) 'repeated reviewer truncation stays bounded and fails closed'
 Check (@((Api '/api/chat').messages|Where-Object text -eq 'THIS_MUST_NOT_PUBLISH').Count-eq0) 'unreviewed text never reaches conversation history'

 $start=@(Get-Content (Join-Path $providerRoot requests.jsonl)).Count
 Plan @((Verdict 'deny')) $true;Plan @((Calls @((Tool 'saved-before-deny' 'workspace.write' @{path='continuation.md';content='COMPLETED_DRAFT_FACT'}))),(Reply 'REJECTED_CONTINUATION_PROSE'))
 $failed=Message 'KEEP_FAILED_USER_INPUT';$stamp=(Get-Item (Join-Path $cfg.dataRoot drafts/continuation.md)).LastWriteTimeUtc
 Check ($failed.failure_continuation.projection_saved-and$failed.failure_continuation.operations-eq1) 'rejected dialogue commits a facts-only failure segment'
 $record=(Api '/api/archive' @{id=$failed.failure_continuation.record_id}).records[0].data
 Check ($record.kind-eq'turn_outcome'-and$record.input_context.text-eq'KEEP_FAILED_USER_INPUT'-and!$record.outcome.reply_delivered-and$record.outcome.completed_operations[0].receipt.result.written) 'encrypted outcome distinguishes original input, actual write and undelivered reply'
 $beforeWire=@(Get-Content (Join-Path $providerRoot requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json -Depth 90})[$start]
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'restart after failed dialogue'
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'fixture exited'};(Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id} 'restarted backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
 Plan @($final) $true;Plan @((Reply '刚才的草稿已经保存了。'));$continued=Message '继续，刚才做完什么了？'
 $wire=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90;$raw=$wire|ConvertTo-Json -Depth 90 -Compress
 Check ($continued.session_projection.mode-eq'append'-and$raw.Contains('KEEP_FAILED_USER_INPUT')-and$raw.Contains('operation_receipt')-and$raw.Contains('turn_outcome')) 'failure input and completed receipt survive restart into next actor context'
 Check (!$raw.Contains('REJECTED_CONTINUATION_PROSE')-and!$raw.Contains('THIS_MUST_NOT_PUBLISH')-and!$raw.Contains('mixed proposal')) 'rejected prose and rejected tool proposals never enter reusable context'
 $prefix=@($wire.messages|Select-Object -First $beforeWire.messages.Count)
 Check (($prefix|ConvertTo-Json -Depth 90 -Compress)-ceq($beforeWire.messages|ConvertTo-Json -Depth 90 -Compress)) 'old history and failed turn original input keep their exact sent prefix'
 Check ((Get-Item (Join-Path $cfg.dataRoot drafts/continuation.md)).LastWriteTimeUtc-eq$stamp-and$continued.tool_results.Count-eq0) 'next turn neither replays nor rewrites the already completed operation'
 $review=Model-ReviewEvent (ReviewWire)[-1];$context=if($review.event_context-is[string]){$review.event_context|ConvertFrom-Json -Depth 90}else{$review.event_context}
 Check (@($context.authorized_context|Where-Object {$_.data.kind-eq'turn_outcome'-and$_.data.input_context.text-eq'KEEP_FAILED_USER_INPUT'}).Count-eq1) 'reviewer sees the same authorized failure facts as actor'
 Plan @((Verdict),@{fixture_status=503}) $true;Plan @((Calls @((Tool 'memory-before-review-outage' 'memory.remember' @{kind='conversation';text='KEEP_MEMORY_AFTER_REVIEW_OUTAGE';source_ids=@('${event_id}')}))),(Reply 'REJECTED_OUTAGE_PROSE'))
 $outage=Message 'KEEP_USER_AFTER_REVIEW_OUTAGE';Check ($outage.failure_continuation.projection_saved-and$outage.turns.Count-eq2-and$outage.reviews.Count-eq2) 'review transport failure persists facts without retrying the entire actor job'
 $kept=@((Api '/api/memories').memories|Where-Object text -eq 'KEEP_MEMORY_AFTER_REVIEW_OUTAGE')[0];Check ($null-ne$kept) 'previously approved memory write survives later reviewer outage'
 $settings=(Api '/api/dashboard').settings;$settings.contextControl|Add-Member -NotePropertyName sessionProjection -NotePropertyValue $false -Force;$null=Api '/api/settings' @{settings=$settings}
 Plan @($final) $true;Plan @((Reply '之前记下的内容还在。'));$fallback=Message '继续刚才的话'
 $wire=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90;$context=Model-CurrentEvent $wire;$raw=$wire|ConvertTo-Json -Depth 90 -Compress
 Check (!$fallback.session_projection-and@($context.authorized_context|Where-Object {$_.data.kind-eq'turn_outcome'-and$_.data.input_context.text-eq'KEEP_USER_AFTER_REVIEW_OUTAGE'}).Count-eq1-and!$raw.Contains('REJECTED_OUTAGE_PROSE')) 'ordinary retrieval retains failure facts when cached session projection is disabled'
 $null=Api '/api/access' @{id=$kept.id;accessible=$false};Plan @($final) $true;Plan @((Reply '继续当前能看的部分。'));$hidden=Message '继续当前的交流'
 $raw=Get-Content (Join-Path $providerRoot last-request.json) -Raw
 Check (!$raw.Contains('KEEP_MEMORY_AFTER_REVIEW_OUTAGE')-and!$raw.Contains('KEEP_USER_AFTER_REVIEW_OUTAGE')) 'revoked sources suppress derived failure facts rather than leaking them through outcome records'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown';@{completed=$true;checks=$checks;root=$root;paid_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output "Layered review passed: $checks checks. $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
