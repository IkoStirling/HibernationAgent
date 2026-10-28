param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot ProjectedRequest.ps1);$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('native-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='native-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(35);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer native-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 60 -Compress))};Invoke-RestMethod @p}
function Plan($Responses,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {if($_.ContainsKey('fixture_status')){@{response='';http_status=$_.fixture_status}}else{@{response=$_}}})}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Native($Id,$Name,$Arguments){@{fixture_native_calls=@(@{id=$Id;name=$Name;arguments=$Arguments})}}
function Reply{@{version=1;kind='reply';text='这一页先放在这里。';calls=@()}}
function Message{$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='完成这次隔离工具验证'};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function Capture{Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70}
function Receipts($Capture){if($script:protocol-eq'responses'){@((Model-CurrentMessages $Capture)|Where-Object type -eq function_call_output|ForEach-Object {$_.output|ConvertFrom-Json -Depth 50})}else{@((Model-CurrentMessages $Capture)|Where-Object role -eq tool|ForEach-Object {$_.content|ConvertFrom-Json -Depth 50})}}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {if($provider.HasExited){throw 'Provider exited'};Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0]);foreach($protocol in @('chat-completions','responses')){$cfg.profiles+=@{id=$protocol;label=$protocol;protocol=$protocol;enabled=$true;origin=$fixture;model='fixture-model';supportsToolCalls=$true;supportsVision=$true;supportsStreaming=$true;supportsJsonObject=$false}}
 $cfg.profiles+=@{id='review';label='Review fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsVision=$true}
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $png=Get-ChildItem -LiteralPath (Join-Path $cfg.pluginRoot widget/characters/asterlan) -Filter *.png -Recurse|Select-Object -First 1
 Copy-Item -LiteralPath $png.FullName -Destination (Join-Path $cfg.dataRoot materials/native.png)
 foreach($protocol in @('chat-completions','responses')){
  $script:protocol=$protocol;$null=Api '/api/model' @{id=$protocol};$name=$protocol+'.txt'
  Plan @((Native 'final-reply' 'ha_turn_reply' @{text='先坐一会儿。'}));$trace=Message
  Check ($trace.turns.Count-eq1-and(Api '/api/chat').messages[-1].text-eq'先坐一会儿。') "$protocol final native function follows the usual reply review"
  $count=(Api '/api/chat').messages.Count;Plan @((Native 'final-silence' 'ha_turn_silence' @{}));$trace=Message
  Check ($trace.silent-and(Api '/api/chat').messages.Count-eq$count+1) "$protocol final silence creates no assistant dialogue"
  Plan @((Native 'opaque-write' 'ha_workspace_DOT_write' @{path=$name;content='真实执行的草稿。'}),(Reply));$trace=Message;$capture=Capture;$receipts=Receipts $capture
  Check ((Test-Path -LiteralPath (Join-Path $cfg.dataRoot ('drafts/'+$name)))-and$receipts[0].ok-and$receipts[0].source_id) "$protocol native proposal executes and returns a real source"
  Check ($trace.turns.Count-eq2-and$trace.reviews.Count-eq2-and$trace.turns[0].native_calls[0].id-eq'opaque-write') "$protocol reviews native operations before execution"
  Check ($capture.tools.Count-gt0) "$protocol advertises callable functions explicitly"
  Plan @((Native 'opaque-image' 'ha_workspace_DOT_view_US_image' @{mount='materials';path='native.png'}),(Reply));$trace=Message;$capture=Capture;$receipts=Receipts $capture
  Check ($receipts[0].ok-and$receipts[0].result.image_attached) "$protocol image observation returns a native receipt"
  $messages=if($protocol-eq'responses'){$capture.input}else{$capture.messages};$last=$messages[-1].content
  Check ($last.Count-eq2-and$last[1].type-in@('input_image','image_url')) "$protocol image is a separate actual image input after receipts"
  $review=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 60
  Check ($review.messages[-1].content[1].type-eq'image_url') "$protocol reviewer receives actual image too"
  Plan @((Native 'bad-name' 'ha_undeclared' @{path='never.txt'}),(Reply));$trace=Message
  Check ($trace.turns.Count-eq2-and$trace.turns[0].status-ne0-and!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot drafts/never.txt))) "$protocol unknown native function is repaired without execution"
  Plan @((Native 'bad-args' 'ha_workspace_DOT_write' @{path='never.txt';content=7}),(Reply));$trace=Message
  Check ($trace.turns.Count-eq2-and$trace.turns[0].status-ne0) "$protocol invalid parameter types never reach execution"
  $mixed=Native 'bad-with-narration' 'ha_undeclared' @{path='never.txt'};$mixed.fixture_native_text='我已经把它写好了。';Plan @($mixed,(Reply));$trace=Message
  Check ($trace.turns.Count-eq2-and!$trace.turns[0].plain_reply_wrapped-and!$trace.turns[0].envelope_normalized-and$trace.reviews.Count-eq1) "$protocol narration beside an invalid native proposal cannot become an approved plain reply"
  Plan @((Native 'traversal' 'ha_workspace_DOT_write' @{path='../protected.txt';content='forbidden'}),(Reply));$null=Message;$receipts=Receipts (Capture)
  Check (!$receipts[0].ok-and!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot protected.txt))) "$protocol valid tool schema does not bypass workspace permissions"
  Plan @(@{decision='rewrite';reason='tool_intent';guidance='不要写文件。'},@{decision='approve';reason='ok';guidance=''}) $true
  Plan @((Native 'rejected' 'ha_workspace_DOT_write' @{path=('rejected-'+$name);content='not approved'}),(Reply));$null=Message
  Check (!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot ('drafts/rejected-'+$name)))) "$protocol rejected proposal has no file effect"
  Plan @(@{decision='approve';reason='ok';guidance=''}) $true
  $remember=@{kind='inference';text=('原生重试只保留一次-'+$protocol);source_ids=@('${event_id}')}
  Plan @((Native 'provider-before-retry' 'ha_memory_DOT_remember' $remember),@{fixture_status=503},(Native 'provider-after-retry' 'ha_memory_DOT_remember' $remember),(Reply));$trace=Message
  $memories=@((Api '/api/memories').memories|Where-Object text -eq $remember.text)
  Check ($memories.Count-eq1) "$protocol changed provider ID on retry does not duplicate memory"
  $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0]
  Check ($job.attempts-eq2) "$protocol fixture exercises a real second job attempt"
  $first=@{kind='inference';text=('中断前已写入-'+$protocol);source_ids=@('${event_id}')};$next=@{kind='inference';text=('中断后继续另一项-'+$protocol);source_ids=@('${event_id}')}
  Plan @((Native 'already-completed' 'ha_memory_DOT_remember' $first),@{fixture_status=503},(Native 'continue-different-operation' 'ha_memory_DOT_remember' $next),(Reply));$continuation=Message
  $saved=@((Api '/api/memories').memories);Check (@($saved|Where-Object text -eq $first.text).Count-eq1-and@($saved|Where-Object text -eq $next.text).Count-eq1) "$protocol transient retry can continue a different operation without colliding with an earlier batch slot"
  $left=@{kind='inference';text=('重排前左项-'+$protocol);source_ids=@('${event_id}')};$right=@{kind='inference';text=('重排前右项-'+$protocol);source_ids=@('${event_id}')}
  $pair=@{fixture_native_calls=@(@{id='pair-a';name='ha_memory_DOT_remember';arguments=$left},@{id='pair-b';name='ha_memory_DOT_remember';arguments=$right})};$reversed=@{fixture_native_calls=@(@{id='pair-b-retry';name='ha_memory_DOT_remember';arguments=$right},@{id='pair-a-retry';name='ha_memory_DOT_remember';arguments=$left})}
  Plan @($pair,@{fixture_status=503},$reversed,(Reply));$reordered=Message;$saved=@((Api '/api/memories').memories)
  Check (@($saved|Where-Object text -eq $left.text).Count-eq1-and@($saved|Where-Object text -eq $right.text).Count-eq1-and$reordered.native_execution_format-eq2) "$protocol reordered native batch replays each original effect once"
  $legacy=@{kind='inference';text=('旧槽位继续-'+$protocol);source_ids=@('${event_id}')};$legacyTurn=@{version=1;kind='call';text=$null;calls=@(@{id='n1_0';tool='memory.remember';args=$legacy})}
  Plan @($legacyTurn,@{fixture_status=503},(Native 'new-provider-old-request' 'ha_memory_DOT_remember' $legacy),(Reply));$legacyTrace=Message
  Check (@((Api '/api/memories').memories|Where-Object text -eq $legacy.text).Count-eq1-and$legacyTrace.native_execution_format-eq1) "$protocol existing legacy slot is pinned and preserved instead of reexecuted during upgrade"
  Check ($legacyTrace.tool_results[0].provider_call_id-eq'new-provider-old-request'-and$legacyTrace.tool_results[0].call_id-eq'n1_0') "$protocol audit binds the actual provider call ID to the stable legacy operation"
  $repair=@{kind='inference';text=('审核改写复用已执行操作-'+$protocol);source_ids=@('${event_id}')}
  Plan @(@{decision='approve';reason='ok';guidance=''},@{decision='rewrite';reason='other';guidance='只修改对白，保留实际操作。'},@{decision='approve';reason='ok';guidance=''},@{decision='approve';reason='ok';guidance=''}) $true
  Plan @((Native 'repair-first' 'ha_memory_DOT_remember' $repair),(Reply),(Native 'repair-second' 'ha_memory_DOT_remember' $repair),(Reply));$trace=Message
  Check (@((Api '/api/memories').memories|Where-Object text -eq $repair.text).Count-eq1-and$trace.tool_results[1].reused_after_review) "$protocol review repair reuses earlier memory effect despite a new call ID"
  $reviewRequest=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 70
  $reviewText=if($reviewRequest.messages){$reviewRequest.messages[1].content}else{$reviewRequest.input[1].content[0].text};if($reviewText-isnot[string]){$reviewText=$reviewText[0].text}
  $facts=($reviewText|ConvertFrom-Json -Depth 65).program_executed_operations
  Check ($facts.Count-eq1-and$facts[0].tool-eq'memory.remember'-and$facts[0].result.id) "$protocol reviewer receives typed program facts instead of inferring execution from wording"
  $old=@((Api '/api/chat').messages|Where-Object role -eq assistant)[-1];$oldRecord=Api '/api/archive' @{id=$old.id}
  Plan @(@{decision='approve';reason='ok';guidance=''},@{decision='reset';reason='persona';guidance='重建旧对白的工作上下文，保留已完成的草稿。'},@{decision='approve';reason='ok';guidance=''}) $true
  Plan @((Native 'before-reset' 'ha_workspace_DOT_write' @{path=('reset-'+$name);content='重建前实际完成。'}),(Reply),(Native 'after-reset' 'ha_turn_reply' @{text='这个新念头先留着。'}));$trace=Message;$rebuilt=Capture
  $generation=$trace.dialogue_context_generation;$rebuiltContext=($rebuilt.input??$rebuilt.messages)[1].content|ConvertFrom-Json -Depth 55
  Check ($trace.context_rebuilds-eq1-and$trace.tool_results.Count-eq1-and(Test-Path -LiteralPath (Join-Path $cfg.dataRoot ('drafts/reset-'+$name)))) "$protocol rebuilding preserves completed effects without executing them twice"
  Check (@($rebuiltContext.authorized_context|Where-Object {$_.data.kind-eq'assistant_message'}).Count-eq0-and@($rebuiltContext.authorized_context|Where-Object {$_.data.kind-eq'user_message'}).Count-gt0) "$protocol rebuild removes old assistant wording while retaining user context"
  Plan @(@{decision='approve';reason='ok';guidance=''}) $true
  Plan @((Native 'next-after-reset' 'ha_turn_reply' @{text='继续刚才的话题。'}));$nextTrace=Message;$nextContext=(($capture=Capture).input??$capture.messages)[1].content|ConvertFrom-Json -Depth 55
  Check ($nextTrace.dialogue_context_generation-eq$generation-and@($nextContext.authorized_context|Where-Object id -eq $old.id).Count-eq0-and@($nextContext.authorized_context|Where-Object {$_.data.text-eq'这个新念头先留着。'}).Count-eq1) "$protocol next turn retains the rebuilt projection and includes newly approved speech"
  Check (((Api '/api/archive' @{id=$old.id})|ConvertTo-Json -Depth 50 -Compress)-eq($oldRecord|ConvertTo-Json -Depth 50 -Compress)) "$protocol context rebuilding never edits the original dialogue archive"
 }
 Plan @((Native 'before-operator' 'ha_state_DOT_propose' @{activity='withdrawing';intensity=-.5;focus='还没写完的便签';source_id='${event_id}'}),(Reply));$null=Message
 $stateBefore=Api '/api/state';$memoryBefore=(Api '/api/memories').memories.Count;$visibleBefore=(Api '/api/chat').messages.Count;$latest=(Api '/api/chat').messages[-1];$latestRaw=Api '/api/archive' @{id=$latest.id}
 Check ($stateBefore.continuous_state.focus-eq'还没写完的便签') 'operator rebuild fixture begins with a genuine nondefault continuous focus'
 $rebuiltByOperator=Api '/api/dialogue/rebuild' @{};$generation=$rebuiltByOperator.generation
 Check ($rebuiltByOperator.rebuilt-and$rebuiltByOperator.history_retained-and$rebuiltByOperator.state_retained-and(Api '/api/chat').messages.Count-eq$visibleBefore) 'operator rebuild preserves visible conversation instead of deleting history'
 $stateAfter=Api '/api/state';Check ($stateAfter.persona_version-eq$stateBefore.persona_version-and$stateAfter.stage.id-eq$stateBefore.stage.id-and$stateAfter.continuous_state.focus-eq$stateBefore.continuous_state.focus-and(Api '/api/memories').memories.Count-eq$memoryBefore) 'operator rebuild preserves persona stage remembered material and live focus'
 Plan @((Native 'after-operator' 'ha_turn_reply' @{text='从这里继续。'}));$manualTrace=Message;$manualCapture=Capture;$manualContext=($manualCapture.input??$manualCapture.messages)[1].content|ConvertFrom-Json -Depth 55
 Check ($manualTrace.dialogue_context_generation-eq$generation-and@($manualContext.authorized_context|Where-Object {$_.data.kind-eq'assistant_message'}).Count-eq0-and@($manualContext.authorized_context|Where-Object {$_.data.kind-eq'user_message'}).Count-gt0) 'operator rebuild affects next automatic dialogue assembly while retaining user facts'
 Check (((Api '/api/archive' @{id=$latest.id})|ConvertTo-Json -Depth 50 -Compress)-eq($latestRaw|ConvertTo-Json -Depth 50 -Compress)) 'operator rebuild keeps exact original archive revisions'
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=(Reply);delay_ms=500})}|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='处理中不能改写当前工作投影'};try{$null=Api '/api/dialogue/rebuild' @{};throw 'Busy rebuild was accepted'}catch{Check ($_.Exception.Response.StatusCode.value__-eq409) 'busy foreground turn rejects operator rebuild'}
 Await {!(Api '/api/chat').busy} 'busy rebuild fixture'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'context projection checkpoint shuts down cleanly'
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Backend exited after restart'};(Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id} 'context projection restart';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 Plan @((Native 'restart-after-reset' 'ha_turn_reply' @{text='没有把旧说法装回来。'}));$restartTrace=Message;$restartCapture=Capture;$restartContext=($restartCapture.input??$restartCapture.messages)[1].content|ConvertFrom-Json -Depth 55
 Check ($restartTrace.dialogue_context_generation-eq$generation-and@($restartContext.authorized_context|Where-Object id -eq $old.id).Count-eq0) 'encrypted context projection survives a real process restart'
 $chat=Api '/api/chat';Check (@($chat.messages|Where-Object {$_.text-like'*fixture_native_calls*'-or$_.text-like'*ha_workspace*'}).Count-eq0) 'native control traffic stays outside visible dialogue'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'native fixture shuts down cleanly'
 @{checks=$checks;completed=$true;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Native tools passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
