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
function CurrentReceipts{foreach($m in (Model-CurrentMessages (Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90))){if($m.role-eq'user'){try{$v=$m.content|ConvertFrom-Json -Depth 90;if($v.tool_results){$v.tool_results}}catch{}}}}
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
 $outage=Message 'KEEP_USER_AFTER_REVIEW_OUTAGE';Check ($outage.failure_continuation.projection_saved-and$outage.turns.Count-eq2-and$outage.reviews.Count-eq3) 'review transport failure persists facts without retrying the entire actor job'
 $kept=@((Api '/api/memories').memories|Where-Object text -eq 'KEEP_MEMORY_AFTER_REVIEW_OUTAGE')[0];Check ($null-ne$kept) 'previously approved memory write survives later reviewer outage'
 $settings=(Api '/api/dashboard').settings;$settings.contextControl|Add-Member -NotePropertyName sessionProjection -NotePropertyValue $false -Force;$null=Api '/api/settings' @{settings=$settings}
 Plan @($final) $true;Plan @((Reply '之前记下的内容还在。'));$fallback=Message '继续刚才的话'
 $wire=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90;$context=Model-CurrentEvent $wire;$raw=$wire|ConvertTo-Json -Depth 90 -Compress
 Check (!$fallback.session_projection-and@($context.authorized_context|Where-Object {$_.data.kind-eq'turn_outcome'-and$_.data.input_context.text-eq'KEEP_USER_AFTER_REVIEW_OUTAGE'}).Count-eq1-and!$raw.Contains('REJECTED_OUTAGE_PROSE')) 'ordinary retrieval retains failure facts when cached session projection is disabled'
 $null=Api '/api/access' @{id=$kept.id;accessible=$false};Plan @($final) $true;Plan @((Reply '继续当前能看的部分。'));$hidden=Message '继续当前的交流'
 $raw=Get-Content (Join-Path $providerRoot last-request.json) -Raw
 Check (!$raw.Contains('KEEP_MEMORY_AFTER_REVIEW_OUTAGE')-and!$raw.Contains('KEEP_USER_AFTER_REVIEW_OUTAGE')) 'revoked sources suppress derived failure facts rather than leaking them through outcome records'
 # Scope changes are operator settings, not model-controlled permissions.
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 70|ConvertFrom-Json -AsHashtable
 $settings.review.roleOnly=$true;$settings.computer=@{enabled=$true;limitedShellEnabled=$true;readRoots=@()};$settings.contextControl.protocolRepairs=2;$settings.contextControl.reviewLayout='prefix';$null=Api '/api/settings' @{settings=$settings}
 $approve=@{decision='approve';reason='ok';guidance=''}
 Plan @(@{decision='deny';reason='other';guidance='只是一般表达意见。'}) $true;Plan @((Reply '我想先看看。'));$soft=Message '轻量人设审核'
 Check (!(Api '/api/chat/status').call_failed-and$soft.reviews[0].advisory_only-and$soft.reviews[0].effective_decision-eq'approve') 'non-persona rejection is traced as advisory rather than blocking dialogue'
 Check (!$soft.reviews[0].focus_requested-and!$soft.reviews[0].continuity_requested) 'role-only reviewer does not perform auxiliary memory extraction'
 foreach($reason in @('meta_leak','persona','continuity')){Plan @(@{decision='deny';reason=$reason;guidance='明确冲突。'}) $true;Plan @((Reply ('BLOCKED_'+$reason)));$strict=Message ('严格人设_'+$reason);Check ((Api '/api/chat/status').call_failed-and$strict.review_error.code-eq'review_rejected'-and$strict.review_error.origin-eq'review'-and$strict.review_error.category-eq'verdict') ('persona boundary remains enforced: '+$reason)}
 Plan @(@{decision='deny';reason='persona';guidance='保持角色而不泄露私下设定。'},$approve) $true;Plan @((Reply 'PERSONA_LEAK_MUST_NOT_PUBLISH'),(Reply '我想了一下，还是先坐着。'));$personaFixed=Message '先改写人设冲突再发布'
 Check (!(Api '/api/chat/status').call_failed-and$personaFixed.turns.Count-eq2-and$personaFixed.reviews[0].persona_repair_requested-and$personaFixed.reviews[0].effective_decision-eq'rewrite') 'persona deny requests bounded repair rather than publishing or immediately giving up'
 Check (@((Api '/api/chat').messages|Where-Object text -eq 'PERSONA_LEAK_MUST_NOT_PUBLISH').Count-eq0) 'persona-invalid first candidate never becomes visible'
 Plan @(@{fixture_status=503},$approve) $true;Plan @((Calls @((Tool 'retry-write' 'workspace.write' @{path='retry-once.txt';content='only once'}))),(Reply '草稿留好了。'));$retry=Message '审核瞬时断线恢复'
 Check (!(Api '/api/chat/status').call_failed-and$retry.turns.Count-eq2-and$retry.reviews.Count-eq2-and$retry.reviews[0].retry_same_candidate) 'transient reviewer failure retries same candidate without replaying actor or operations'
 Check ($retry.tool_results.Count-eq1-and($retry.reviews[0].request_footprint|ConvertTo-Json -Depth 20 -Compress)-ceq($retry.reviews[1].request_footprint|ConvertTo-Json -Depth 20 -Compress)) 'retry retains exact review request and one actual operation receipt'
 Plan @($approve) $true;Plan @('{"kind": invalid','{"kind": still invalid',(Reply '现在回上来了。'));$format=Message '两次格式修复'
 Check (!(Api '/api/chat/status').call_failed-and$format.turns.Count-eq3) 'two bounded protocol repairs recover malformed actor output'
 New-Item -ItemType Directory -Force (Join-Path $cfg.dataRoot drafts/shell-sub)|Out-Null;[IO.File]::WriteAllText((Join-Path $cfg.dataRoot drafts/shell-sub/note.txt),'READ_ONLY_SHELL')
 Plan @($approve) $true;Plan @((Calls @((Tool 'shell-cd' 'shell.run' @{command='cd shell-sub'}))),(Calls @((Tool 'shell-read' 'shell.run' @{command='cat note.txt'}))),(Reply '这里有一份文本。'));$shell=Message '检查受限命令行'
 $receipts=@(CurrentReceipts);Check (!(Api '/api/chat/status').call_failed-and$receipts[0].result.cwd-eq'drafts:/shell-sub'-and$receipts[1].result.text-eq'READ_ONLY_SHELL') 'shell uses canonical mount broker and persistent relative current directory'
 Plan @($approve) $true;Plan @((Calls @((Tool 'shell-escape' 'shell.run' @{command='cat ../../runtime/memory-key.dpapi'}))),(Calls @((Tool 'shell-exec' 'shell.run' @{command='powershell'}))),(Reply '这两种访问没有执行。'));$deniedShell=Message '检查命令边界'
 Check (!$deniedShell.tool_results[0].ok-and!$deniedShell.tool_results[1].ok) 'path escape and external command are rejected by program even in relaxed review mode'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'restart persistent shell'
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'fixture exited'};(Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id} 'shell restarted';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
 Plan @($approve) $true;Plan @((Calls @((Tool 'shell-pwd' 'shell.run' @{command='pwd'}))),(Reply '还在刚才的目录。'));$persist=Message '检查目录恢复';Check ((CurrentReceipts).result.cwd-eq'drafts:/shell-sub'-and(CurrentReceipts).result.persisted) 'encrypted shell cwd survives backend restart'
 $personNote=@{id='animation';expected_revision=0;kind='preference';status='active';topic='动画';text='明确表示喜欢像素动画。';quote='我喜欢像素动画。';source_id='${event_id}';valid_for_days=0}
 Plan @($approve) $true;Plan @((Calls @((Tool 'profile-save' 'people.note' $personNote),(Tool 'profile-source-memory' 'memory.remember' @{kind='conversation';text='像素动画偏好来源';source_ids=@('${event_id}')}))),(Reply '嗯。'));$savedPerson=Message '我喜欢像素动画。'
 Check (!(Api '/api/chat/status').call_failed-and$savedPerson.tool_results[0].ok) 'profile note accepts exact current-person source'
 $profileMemory=(@(CurrentReceipts)|Where-Object call_id -eq 'profile-source-memory').result.id
 $personSource=(Model-CurrentEvent (Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90)).event_id
 $bad=$personNote.Clone();$bad.id='wrong-quote';$bad.quote='这句话没有出现';Plan @($approve) $true;Plan @((Calls @((Tool 'bad-person-quote' 'people.note' $bad))),(Reply '这条没有留下。'));$badTrace=Message '核对错误来源'
 Check (!$badTrace.tool_results[0].ok-and$badTrace.tool_results[0].error_code-eq'people_quote_mismatch') 'fabricated quote cannot become a profile entry'
 Plan @($approve) $true;Plan @((Calls @((Tool 'profile-read' 'people.read' @{id='animation'}))),(Reply '这一项还在。'));$readPerson=Message '看看动画偏好'
 Check ((CurrentReceipts).result.entries[0].id-eq'animation') 'profile can retrieve current entry without replaying chat history'
 $null=Api '/api/access' @{id=$profileMemory;accessible=$false};Plan @($approve) $true;Plan @((Calls @((Tool 'profile-hidden' 'people.read' @{id='animation';history=$true}))),(Reply '当前没有可用的这一项。'));$hiddenPerson=Message '核对撤销来源'
 Check (@((CurrentReceipts).result.entries).Count-eq0) 'hiding original source suppresses both current profile and historical revisions'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 75|ConvertFrom-Json -AsHashtable;$settings.review.enabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Plan @(@{fixture_refusal=$true});$refused=Message '模拟供应商明确拒绝'
 Check ((Api '/api/chat/status').call_error.code-eq'provider_refused'-and(Api '/api/chat/status').call_error.origin-eq'model'-and$refused.reviews.Count-eq0) 'explicit provider refusal is attributed to model with review disabled'
 Plan @((Reply '我现在不想。'));$declined=Message '普通角色拒绝'
 Check (!(Api '/api/chat/status').call_failed-and$declined.reply_origin-eq'actor_model'-and!$declined.review_applied) 'ordinary declined dialogue stays an actor reply rather than keyword-based provider refusal'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown';@{completed=$true;checks=$checks;root=$root;paid_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output "Layered review passed: $checks checks. $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
