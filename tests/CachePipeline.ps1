param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
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
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.maxToolRounds=3;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Cache actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsToolCalls=$true;supportsStreaming=$true;supportsJsonObject=$true},@{id='review';label='Cache review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $root config.json) -Encoding utf8
 @{prompt_tokens=100;completion_tokens=10;prompt_cache_hit_tokens=80;prompt_cache_miss_tokens=20}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $providerRoot usage.json) -Encoding utf8
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true;Start-Backend;$null=Api '/api/model' @{id='actor'}
 New-Item -ItemType Directory -Force (Join-Path $cfg.dataRoot drafts)|Out-Null;[IO.File]::WriteAllText((Join-Path $cfg.dataRoot drafts/prefix.txt),('h'*8000))
 Plan @((Native 'a' 'ha_workspace_DOT_read' @{mount='drafts';path='prefix.txt';length=8192}),(Native 'b' 'ha_state_DOT_observe' @{}),(Native 'c' 'ha_state_DOT_observe' @{}),(Native 'final' 'ha_turn_reply' @{text='三次核对完成。'}));$trace=Message '读取当前状态三次'
 Check (!(Api '/api/chat').call_failed-and$trace.turns.Count-eq4) 'native three-action turn completes with independent final review'
 $wire=@(Get-Content (Join-Path $providerRoot requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json -Depth 80});$definitions=$wire[0].tools|ConvertTo-Json -Depth 75 -Compress
 Check (@($wire|Where-Object {($_.tools|ConvertTo-Json -Depth 75 -Compress)-cne$definitions}).Count-eq0) 'native tool definitions stay byte-equivalent through the final round'
 $receipt=$wire[1].messages|Where-Object role -eq tool|Select-Object -First 1;$laterReceipt=$wire[2].messages|Where-Object role -eq tool|Select-Object -First 1
 Check ($receipt.content.Length-gt8000-and$receipt.content-ceq$laterReceipt.content) 'an already sent eight-kilobyte receipt stays identical on the next action step'
 $reviewWire=Get-Content (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 80;$reviewFrame=Model-ReviewEvent $reviewWire
 Check ($reviewFrame.event_context.StartsWith('{"event_kind"')) 'review projection retains cache ordering inside the nested serialized event'
 Check ($trace.turns[0].usage.cached_input_tokens-eq80-and$trace.reviews[0].usage.uncached_input_tokens-eq20) 'actor streaming and reviewer usage reach encrypted traces'
 $system=$wire[0].messages[0].content;$frame=$wire[0].messages[1].content|ConvertFrom-Json -Depth 65
 Check (!$system.Contains('"observed_at_ms"')-and$frame.authoritative_state-and$frame.current_capabilities-and$trace.cache_layout.dynamic_state_in_system-eq$false) 'dynamic facts remain available outside the stable system prompt'
 $stats=Api '/api/model-cache';Check ($stats.connections.actor.total.cached_input_tokens-eq320-and$stats.connections.review.total.cached_input_tokens-eq80-and$stats.connections.actor.total.cache_hit_ratio-eq.8) 'cache accounting separates actor and reviewer connections'
 Remove-Item -LiteralPath (Join-Path $providerRoot usage.json)
 Plan @((Native 'final2' 'ha_turn_reply' @{text='仍记得刚才在核对状态。'}));$second=Message '再说一句';$last=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 80
 Check ($last.messages[0].content-ceq$system) 'system text stays identical across changed timestamps and conversation state'
 Check ($second.turns[0].usage.cached_input_tokens-eq$null-and(Api '/api/model-cache').connections.actor.total.cache_reported_calls-eq4) 'missing provider cache metrics are unknown and do not count as zero-hit calls'
 Plan @((Native 'd' 'ha_state_DOT_observe' @{}),(Native 'e' 'ha_state_DOT_observe' @{}),(Native 'f' 'ha_state_DOT_observe' @{}),(Native 'forbidden' 'ha_workspace_DOT_write' @{path='must-not-exist.md';content='forbidden'}));$blocked=Message '验证最后阶段的操作限制'
 Check ((Api '/api/chat').call_error.code-eq'tool_round_limit'-and!(Test-Path (Join-Path $cfg.dataRoot drafts/must-not-exist.md))) 'stable visible tools never allow an action beyond the execution limit'
 $saved=Api '/api/model-cache';Stop-Backend;Start-Backend;$restored=Api '/api/model-cache';Check ($restored.connections.actor.total.calls-eq$saved.connections.actor.total.calls-and$restored.connections.review.total.calls-eq$saved.connections.review.total.calls) 'usage persists without replaying calls on restart'
 Stop-Backend;@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Cache pipeline passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
