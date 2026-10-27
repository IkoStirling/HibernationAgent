param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/television-sequence-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='sequence-fixture-only';$provider=$null;$service=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer sequence-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function ReviewPlan($Decisions){
 $responses=@($Decisions|ForEach-Object {
  $verdict=@{decision=$_;reason='ok';guidance=''}
  if($_-ne'approve'){$verdict.reason='continuity';$verdict.guidance='重新接话，保留已完成的实际观察。'}
  @{response=$verdict}
 })
 @{nonce=[Guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $root provider/review-plan.json) -Encoding utf8
}
function Reply{@{version=1;kind='reply';text='这几帧的变化先记在这里。';calls=@()}}
function Call($Id,$Count=3){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool='television.observe_sequence';args=@{frames=$Count;spacing_ms=250}})}}
function Message($Items){Plan $Items;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='看看这个视频当前几帧的变化'};Await {!(Api '/api/chat').busy} 'role turn';Check (!(Api '/api/chat').call_failed) 'role sequence turn completes';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function Capture{Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 65}
function Feedback{((Capture).messages[-1].content[0].text|ConvertFrom-Json -Depth 55).tool_results[0]}
function Plugin($Name,$Arguments=@{}){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Await {(Api '/api/plugin/result').state-in@('succeeded','failed')} $Name;(Api '/api/plugin/result').result}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins
 $cfg.frontend.startupAnimation=$false;$cfg.computer=@{enabled=$false};$cfg.plugins=@{television=@{enabled=$true;volume=0;wakeOnEnd=$false}};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Sequence fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true;supportsVision=$true},@{id='reviewer';label='Review fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true;supportsVision=$true});$cfg.review=@{enabled=$true;profileId='reviewer';maxRewrites=1}
 # This suite explicitly exercises full-review reset and image retention.
 $cfg.contextControl=@{preset='full'}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root provider/review-plan.json) -Encoding utf8
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$service=$desktop.Child;Await {Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 Copy-Item -LiteralPath (Join-Path $root provider/clip.mp4) -Destination (Join-Path $cfg.dataRoot materials/sequence.mp4)
 $opened=Plugin 'television.open' @{source='sequence.mp4'};Check $opened.ok 'sequence uses the existing engine local player'
 $again=Plugin 'television.control' @{action='play'};Check ($again.ok-and(Plugin 'television.observe' @{include_frame=$false}).result.state-eq'playing') 'repeated play is an application no-op and never corrupts actual engine state'
 $trace=Message @((Call 'frames'),(Reply));$feedback=Feedback;$capture=Capture;$frames=@($feedback.result.frames)
 $operator=@((Api '/api/capabilities').plugins|Where-Object id -eq television)[0].current
 Check (!$capture.messages[0].content.Contains('"screen_rect"')-and!$capture.messages[0].content.Contains('"sync_source"')-and$operator.screen_rect-and$operator.sync_source) 'role world omits rendering diagnostics while operator state retains them'
 Check ($feedback.ok-and$frames.Count-eq3-and$feedback.result.sampled_frames-eq3) 'one role call samples three actual advancing frames'
 Check ($frames[0].frame_time_seconds-lt$frames[1].frame_time_seconds-and$frames[1].frame_time_seconds-lt$frames[2].frame_time_seconds) 'frame times are ordered actual playback times'
 Check ($feedback.result.span_seconds-ge.3-and!$feedback.result.full_video_watched-and!$feedback.result.audio_delivered) 'small observed range never claims full-film or audio perception'
 Check ($capture.messages[-1].content.Count-eq4-and$frames[0].image_part_index-eq0-and$frames[2].image_part_index-eq2) 'ordered image parts map to frame metadata without base64 text'
 Check (($feedback|ConvertTo-Json -Depth 55).IndexOf('base64')-lt0) 'model text receipt excludes all sampled pixel bytes'
 $review=Get-Content -LiteralPath (Join-Path $root provider/review-last-request.json) -Raw|ConvertFrom-Json -Depth 65
 Check ($review.messages[-1].content.Count-eq4-and$review.messages[-1].content[3].image_url.url-eq$capture.messages[-1].content[3].image_url.url) 'reviewer receives the same ordered sampled pictures'
 Check ($trace.visual_inputs.Count-eq3-and$trace.visual_inputs[2].frame_index-eq2) 'per-frame delivery remains traceable'
 $source=$feedback.source_id;$raw=Api '/api/archive' @{id=$source};Check ($raw.records[0].data.result.frames[0].image.base64.Length-gt100) 'encrypted source retains actual sampled pixels'
 $null=Message @((Reply));$initial=(Capture).messages[1].content|ConvertFrom-Json -Depth 55;$past=@($initial.authorized_context|Where-Object id -eq $source)
 Check ($past.Count-eq1-and$past[0].data.result.images_delivered_to_model-and$past[0].data.result.frames.Count-eq3-and($past[0]|ConvertTo-Json -Depth 55).IndexOf('base64')-lt0) 'next conversation recalls observed range without replaying video pixels'
 $null=Plugin 'television.control' @{action='pause'};$paused=Plugin 'television.observe_sequence' @{frames=3;spacing_ms=250}
 Check ($paused.ok-and$paused.result.sampled_frames-eq1-and$paused.result.stop_reason-eq'playback_not_advancing') 'paused video returns one real frame instead of invented changes'
 $null=Plugin 'television.control' @{action='seek';seconds=0};$null=Plugin 'television.control' @{action='play'}
 $trace=Message @((Call 'once' 2),(Call 'twice' 2),(Reply));Check (@($trace.tool_results|Where-Object error_code -eq media_attempt_limit_use_existing_results).Count-eq1) 'second sequence request in one turn is bounded without another sample loop'
 $null=Plugin 'television.control' @{action='seek';seconds=0};$null=Plugin 'television.control' @{action='play'};ReviewPlan @('approve','approve','reset','approve')
 $single=@{version=1;kind='call';text=$null;calls=@(@{id='single';tool='television.observe';args=@{include_frame=$true}})};$trace=Message @($single,(Call 'newest' 2),(Reply),(Reply));$capture=Capture
 $actualParts=@($capture.messages|Where-Object {$_.content-isnot[string]}|ForEach-Object {$_.content}|Where-Object type -eq image_url)
 Check ($trace.reviews[-2].verdict.decision-eq'reset'-and$actualParts.Count-eq2-and$trace.tool_results.Count-eq2) 'review reset retains only newest frame batch without resurrecting pixels or repeating tools';ReviewPlan @('approve')
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.profiles[1].supportsVision=$false;$null=Api '/api/settings' @{settings=$settings}
 $null=Message @((Call 'no-vision' 2),(Reply));$last=(Capture).messages[-1].content;if($last-isnot[string]){$last=$last[0].text};$disabled=($last|ConvertFrom-Json -Depth 55).tool_results[0]
 Check (!$disabled.result.frames[0].image_attached-and$disabled.result.frames[0].image_unavailable_reason-eq'vision_not_enabled'-and($disabled|ConvertTo-Json -Depth 55).IndexOf('base64')-lt0) 'text-only connection receives honest metadata and no hidden image bytes'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'sequence host exits cleanly'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "TV sequence passed: $checks checks. Evidence: $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(5000)}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
}
