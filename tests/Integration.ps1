param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$checks=0;$service=$null;$provider=$null
$root=Join-Path $ScratchRoot ([Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='integration-local-session-fixture';$env:HIBERNATION_TEST_PROVIDER_KEY='integration-provider-fixture-not-a-real-key'
$headers=@{Authorization='Bearer '+$env:HIBERNATION_SESSION_KEY};$readyPath=Join-Path $root 'ready.json';$configPath=Join-Path $root 'config.json'
function Check([bool]$Condition,[string]$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,[string]$Name,[int]$Seconds=20){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow -lt $end){if(& $Probe){return};Start-Sleep -Milliseconds 80};throw "TIMEOUT: $Name"}
function Call([string]$Path,$Data=$null){$argsMap=@{Uri=($script:origin+$Path);Headers=$headers;TimeoutSec=10};if($null-ne$Data){$argsMap.Method='Post';$argsMap.ContentType='application/json; charset=utf-8';$argsMap.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 25 -Compress))};Invoke-RestMethod @argsMap}
function Status([string]$Path,[int]$Expected,$CustomHeaders=$headers){$r=Invoke-WebRequest -Uri ($script:origin+$Path) -Headers $CustomHeaders -SkipHttpErrorCheck -TimeoutSec 5;Check ($r.StatusCode-eq$Expected) "HTTP $Expected $Path"}
function Start-ServiceProcess{
 if(Test-Path -LiteralPath $readyPath){$old=(Get-Item -LiteralPath $readyPath).LastWriteTimeUtc}else{$old=[DateTime]::MinValue}
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$readyPath+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'service.log') -RedirectStandardError (Join-Path $root 'service-error.log')
 Wait-For { $script:service.Refresh();if($script:service.HasExited){throw 'service exited before ready'};if((Test-Path -LiteralPath $readyPath)-and (Get-Item -LiteralPath $readyPath).LastWriteTimeUtc -gt $old){try{$r=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;return $r.pid-eq$script:service.Id}catch{}};return $false } 'ready'
 $r=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;$script:origin="http://127.0.0.1:$($r.port)"
}
function Stop-ServiceProcess{$null=Call '/api/shutdown' @{};$script:service.WaitForExit(15000)|Out-Null;Check $script:service.HasExited 'graceful process exit'}
function Wait-Idle{Wait-For {!(Call '/api/state').busy} 'application idle'}
function Message([string]$Text){$r=Call '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Wait-Idle;return $r}
function Provider-Output($Value){[IO.File]::WriteAllText((Join-Path $root 'provider/response.json'),($Value|ConvertTo-Json -Depth 20 -Compress))}
try{
 $providerRoot=Join-Path $root 'provider';New-Item -ItemType Directory -Path $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'provider.log') -RedirectStandardError (Join-Path $root 'provider-error.log')
 Wait-For {Test-Path -LiteralPath (Join-Path $providerRoot 'port')} 'fixture provider';$providerPort=[int](Get-Content -LiteralPath (Join-Path $providerRoot 'port'))
 $config=Get-Content -LiteralPath (Join-Path $ProjectRoot 'config/example.json') -Raw|ConvertFrom-Json
 $config.dataRoot=Join-Path $root 'data';$config.characterRoot=Join-Path $ProjectRoot 'character';$config.uiRoot=Join-Path $ProjectRoot 'ui';$config.solitudeIntervalMs=600000;$config.idleBeforeSolitudeMs=0
 $config.profiles=@($config.profiles[0],[pscustomobject]@{id='fixture';label='Fixture compatible model';protocol='chat-completions';enabled=$true;origin="http://127.0.0.1:$providerPort";basePath='/v1';model='fixture-model';credentialEnvironment='HIBERNATION_TEST_PROVIDER_KEY';supportsJsonSchema=$false;timeoutMs=10000;maxOutputTokens=2048})
 $config|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $configPath -Encoding utf8
 Start-ServiceProcess
 Status '/' 200 @{};Status '/ui/dashboard-order.js' 200 @{};Status '/ui/dashboard-panels.js' 200 @{};Status '/ui/memory-workshop.js' 200 @{};Status '/api/memory-workshop' 401 @{};Status '/api/state' 401 @{};Status '/api/state' 403 @{Authorization=$headers.Authorization;Origin='http://attacker.invalid'}
 Status '/api/state' 200 @{Authorization=$headers.Authorization;Origin=$script:origin}
 $first=Message '一颗恒星如何理解等待？';$second=Message '故事里有两次不同的相遇，时间差仍然未解。'
 $state=Call '/api/state';Check ($state.messages.Count-eq4) 'two persisted conversations';Check ($state.stage.id-eq'initial') 'stage is separate from attention state'
 $memories=(Call '/api/memories').memories;Check ($memories.Count-eq2) 'memory proposals accepted';Check ($memories[0].sources[0].revision-eq1) 'memory source revision retained'
 $duplicate=Call '/api/message' @{request_id=$first.request_id;text='一颗恒星如何理解等待？'};Check $duplicate.duplicate 'request idempotency';Check ((Call '/api/state').messages.Count-eq4) 'duplicate input not appended twice'
 $null=Call '/api/solitude' @{};Wait-Idle
 Wait-For {((Call '/api/memories').memories|Where-Object origin -eq solitude).Count-gt0} 'solitude memory'
 $reflection=((Call '/api/memories').memories|Where-Object origin -eq solitude)[0];Check ($reflection.kind-eq'inference') 'solitude remains inference, not fact'
 Wait-For {Test-Path -LiteralPath (Join-Path $config.dataRoot 'drafts/reflections')} 'draft outbox'
 $third=Message '接着刚才的思路。';$state=Call '/api/state';Check ($state.messages[-1].text.Contains('上次独处时')) 'subsequent conversation uses independent work'
 $trace=Call '/api/trace' @{id=$state.state.lastTraceId};Check ($trace.selected_ids-contains$reflection.id) 'trace links retrieved solitude memory'
 $null=Call '/api/access' @{id=$memories[0].id;accessible=$false}
 $fourth=Message '现在只看本次允许访问的材料。';$trace=Call '/api/trace' @{id=(Call '/api/state').state.lastTraceId}
 Check ($trace.selected_ids-notcontains$memories[0].id) 'hidden memory excluded before context assembly';Check ($trace.selected_ids-notcontains$first.event_id) 'hidden memory original fragment excluded'
 $raw=Call '/api/archive' @{id=$first.event_id};Check ($raw.records[0].data.text-eq'一颗恒星如何理解等待？') 'original archive survives visibility change'
 $null=Call '/api/access' @{id=$memories[0].id;accessible=$true}
 $null=Call '/api/model' @{id='fixture'}
 $output=@{visible_reply='模型已经更换，经历与阶段继续保留。';actions=@();memories=@();state_update=@{attention='检查跨模型连续性'};stage_proposal=@{stage_id='exploring';reason='引用两次已归档经历，提交操作者审核。';evidence_ids=@($first.event_id,$second.event_id)}}
 Provider-Output $output;$fifth=Message '请检查这两次经历，但不要自行改变阶段。';$state=Call '/api/state'
 Check ($state.messages[-1].text-eq$output.visible_reply) 'compatible provider works without native schema support';Check ($state.stage.id-eq'initial') 'stage proposal does not apply automatically'
 $review=@($state.reviews|Where-Object status -eq pending)[0];Check ($null-ne$review) 'stage proposal preserved for review'
 $null=Call '/api/review' @{id=$review.id;approve=$true};Check ((Call '/api/state').stage.id-eq'exploring') 'human review publishes evidenced stage'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot 'last-request.json') -Raw;Check (!$capture.Contains($env:HIBERNATION_TEST_PROVIDER_KEY)) 'credential absent from model body';Check (!(Call '/api/state'|ConvertTo-Json -Depth 20 -Compress).Contains($env:HIBERNATION_TEST_PROVIDER_KEY)) 'credential absent from public state'
 $output.stage_proposal=$null;$output.actions=@(@{kind='read_material';path='welcome.md';content=''});Provider-Output $output;$null=Message '只读取指定素材，不修改它。'
 Wait-For {@((Call '/api/memories').memories|Where-Object origin -eq material_read).Count-gt0} 'material read outbox'
 $material=@((Call '/api/memories').memories|Where-Object origin -eq material_read)[0];Check ($material.sources[0].scope-eq'ha.actions') 'material snapshot has versioned action provenance'
 $file=Call '/api/workspace' @{mount='materials';path='welcome.md';read=$true};Check ($file.content.Contains('实验素材')) 'read-only material remains readable'
 $denied=Invoke-WebRequest -Uri ($script:origin+'/api/workspace') -Method Post -Body '{"mount":"drafts","path":"../archive/records.sqlite","read":true}' -ContentType 'application/json' -Headers $headers -SkipHttpErrorCheck;Check ($denied.StatusCode-eq403) 'workspace cannot read raw archive through traversal'
 $output.stage_proposal=$null;$output.actions=@(@{kind='write_draft';path='../character/persona.json';content='forbidden'});Provider-Output $output
 $before=Get-Content -LiteralPath (Join-Path $ProjectRoot 'character/persona.json') -Raw;$null=Message '这是一份需要校验的动作提案。';$state=Call '/api/state';$trace=Call '/api/trace' @{id=(@($state.jobs|Where-Object id -eq chat)[0].result|ConvertFrom-Json).trace_id}
 Check ($trace.validation-eq'workspace_path') 'unsafe action rejects entire proposal set';Check ((Get-Content -LiteralPath (Join-Path $ProjectRoot 'character/persona.json') -Raw)-eq$before) 'core persona protected';Check ($state.state.attention-eq'检查跨模型连续性') 'rejected output has no state effects'
 $null=Call '/api/model' @{id='mock'}
 $story=Call '/api/event' @{request_id=[Guid]::NewGuid().ToString('N');kind='story_end';text='故事落点：角色完成了等待，随后开始评书。'};Wait-Idle
 Check (@((Call '/api/memories').memories|Where-Object kind -eq story).Count-eq1) 'story-end event creates separately typed memory'
 $events=Call '/api/events' @{channel='agent.solitude'};Check ($events.events.Count-gt0) 'event channel query isolates independent work'
 $null=Call '/api/control' @{paused=$true};Stop-ServiceProcess;Start-ServiceProcess
 $state=Call '/api/state';Check ($state.stage.id-eq'exploring') 'stage persists across actual restart';Check $state.control.paused 'pause persists across actual restart';Check ($state.messages.Count-ge12) 'original conversations persist across restart'
 $null=Call '/api/control' @{paused=$false};$null=Message '重启之后继续。';Check ((Call '/api/state').model.id-eq'mock') 'selected model persists across restart'
 Stop-ServiceProcess
 $log=Get-Content -LiteralPath (Join-Path $root 'service.log') -Raw;Check (!$log.Contains($env:HIBERNATION_SESSION_KEY)) 'session credential absent from log';Check (!$log.Contains($env:HIBERNATION_TEST_PROVIDER_KEY)) 'provider credential absent from log'
 [pscustomobject]@{completed=$true;checks=$checks;root=$root;timestamp=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'acceptance.json') -Encoding utf8
 Write-Output "Integration passed: $checks checks. Evidence: $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Call '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
}
