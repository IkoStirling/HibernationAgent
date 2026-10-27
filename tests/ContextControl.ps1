param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('context-control-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='context-control-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(35);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer context-control-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 70 -Compress))};Invoke-RestMethod @p}
function Plan($Items,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Native($Id,$Name,$Arguments){@{fixture_native_calls=@(@{id=$Id;name=$Name;arguments=$Arguments})}}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function SetPolicy($Control){$settings=(Api '/api/dashboard').settings;$settings.contextControl=$Control;$null=Api '/api/settings' @{settings=$settings}}
function Size($Trace){$sum=0;foreach($call in @($Trace.turns)+@($Trace.reviews)){$f=$call.request_footprint;$sum+=$f.text_bytes+$f.tool_schema_bytes+$f.output_schema_bytes};return $sum}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0]);foreach($protocol in @('chat-completions','responses')){$cfg.profiles+=@{id=$protocol;label=$protocol;protocol=$protocol;enabled=$true;origin=$fixture;model='fixture-model';supportsToolCalls=$true;supportsVision=$true;supportsStreaming=$true;supportsJsonObject=$true}}
 $cfg.profiles+=@{id='review';label='Review';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsVision=$true;supportsJsonObject=$true}
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content -LiteralPath $configPath -Encoding utf8
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Context backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 for($i=0;$i-lt6;$i++){$null=Message ('HISTORY_'+$i+(' context history data'*160));if($i-eq0){$historyId=@((Api '/api/chat').messages|Where-Object role -eq user)[0].id}}
 $png=Get-ChildItem -LiteralPath (Join-Path $cfg.pluginRoot widget/characters/asterlan) -Filter *.png -Recurse|Select-Object -First 1
 Copy-Item -LiteralPath $png.FullName -Destination (Join-Path $cfg.dataRoot materials/context.png)
 $results=@()
 foreach($protocol in @('chat-completions','responses')){
  $null=Api '/api/model' @{id=$protocol}
  $metrics=@{}
  foreach($preset in @('full','balanced')){
   SetPolicy @{enabled=$true;preset=$preset}
   Plan @((Native 'picture' 'ha_workspace_DOT_view_US_image' @{mount='materials';path='context.png'}),(Native 'state' 'ha_state_DOT_observe' @{}),(Native 'final' 'ha_turn_reply' @{text='这是经过审核的回复。'}))
   $trace=Message '查看本次指定图片，查询当前姿态后回应。';$metrics[$preset]=$trace
   Check (!(Api '/api/chat').call_failed-and$trace.turns.Count-eq3) "$protocol $preset native flow completes"
   Check ($trace.turns[1].request_footprint.image_count-eq1) "$protocol $preset image delivered to actor after observation"
   Check ($trace.reviews[-1].request_footprint.image_count-eq1) "$protocol $preset final reviewer retains latest pixels"
  }
  $full=$metrics.full;$small=$metrics.balanced
  Check ($full.reviews.Count-eq3-and$small.reviews.Count-eq1-and$small.reviews_skipped.Count-eq2) "$protocol read-only steps skip semantic review but final dialogue is reviewed"
  Check ($full.turns[2].request_footprint.image_count-eq1-and$small.turns[2].request_footprint.image_count-eq0) "$protocol previously acknowledged image is not sent repeatedly"
  Check ($small.context_bytes-le12000-and$small.selected_ids.Count-gt0) "$protocol assembled history is bounded with sources retained"
  Check ((Size $small)-lt(Size $full)) "$protocol total text and schema bytes decrease"
  Check ($small.turns[0].request_footprint.output_schema_bytes-eq0-and$full.turns[0].request_footprint.output_schema_bytes-gt0) "$protocol native schema is not duplicated"
  $results+=@{protocol=$protocol;full_calls=$full.turns.Count+$full.reviews.Count;balanced_calls=$small.turns.Count+$small.reviews.Count;full_bytes=(Size $full);balanced_bytes=(Size $small)}
  # All writes and final replies still require independent approval.
  Plan @(@{decision='deny';reason='tool_intent';guidance='不要写入。'}) $true
  Plan @((Native 'denied' 'ha_workspace_DOT_write' @{path='denied.txt';content='must not appear'}))
  $trace=Message '这是拒绝审核验证。';Check (!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot drafts/denied.txt))-and$trace.reviews.Count-eq1) "$protocol write rejection remains fail closed"
  Plan @(@{decision='approve';reason='ok';guidance=''}) $true
 }
 SetPolicy @{enabled=$true;preset='balanced';reviewReadOnlyTools=$true}
 Plan @((Native 'state' 'ha_state_DOT_observe' @{}),(Native 'final' 'ha_turn_reply' @{text='完整查询审核。'}));$trace=Message '查询审核可恢复。'
 Check ($trace.reviews.Count-eq2-and$trace.reviews_skipped.Count-eq0) 'explicit strict query review restores all review steps'
 $invalid=(Api '/api/dashboard').settings;$invalid.contextControl=@{historyBytes=-1};$rejected=$false;try{$null=Api '/api/settings' @{settings=$invalid}}catch{$rejected=$true};Check $rejected 'invalid context configuration is rejected without applying it'
 $original=Api '/api/archive' @{id=$historyId};Check ($original.records[0].data.text.StartsWith('HISTORY_')) 'original long history remains archived'
 $results|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root metrics.json) -Encoding utf8
 $results|ConvertTo-Json -Compress;Write-Output "$checks context control checks passed"
}finally{if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(10000)}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id -Force}};if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id -Force}}
