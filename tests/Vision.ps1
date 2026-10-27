param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('vision-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='vision-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer vision-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plan($Responses){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8}
function Calls($Path){@{version=1;kind='call';text=$null;calls=@(@{id='picture';tool='workspace.view_image';args=@{mount='materials';path=$Path}})}}
function Reply{@{version=1;kind='reply';text='收到本次提供的图片。';calls=@()}}
function Message{ $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='读取指定图片以检查输入链路'};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function Feedback{ $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;$last=$capture.messages[-1].content;if($last-isnot[string]){$last=$last[0].text};($last|ConvertFrom-Json).tool_results[0] }
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {if($provider.HasExited){throw 'Provider exited'};Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0],@{id='vision';label='Vision fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsVision=$true;supportsJsonObject=$true},@{id='review';label='Review fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsVision=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $configPath -Encoding utf8
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $providerRoot review-plan.json) -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='vision'}
 # Exact fixture bytes come from an existing transparent sprite; the extension
 # deliberately differs so the actual decoder must determine image content.
 $pack=Get-Content -LiteralPath (Join-Path $cfg.pluginRoot widget/characters/asterlan/character.json) -Raw|ConvertFrom-Json
 $png=Get-ChildItem -LiteralPath (Join-Path $cfg.pluginRoot widget/characters/asterlan) -Filter *.png -Recurse|Select-Object -First 1
 Copy-Item -LiteralPath $png.FullName -Destination (Join-Path $cfg.dataRoot materials/picture.bin)
 [IO.File]::WriteAllText((Join-Path $cfg.dataRoot materials/invalid.png),'not an image')
 [IO.File]::WriteAllText((Join-Path $cfg.dataRoot materials/short.txt),'静夜里留一小段。',[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllBytes((Join-Path $cfg.dataRoot materials/control.bin),[byte[]](65,0,66))
 $textReply=@{version=1;kind='reply';text='这份素材按实际类型处理。';calls=@()}
 foreach($path in @('picture.bin','control.bin')){
  $read=@{version=1;kind='call';text=$null;calls=@(@{id='binary-as-text';tool='workspace.read';args=@{mount='materials';path=$path}})};Plan @($read,$textReply);$null=Message;Check ((Feedback).error.code-eq'workspace_not_text'-and!(Api '/api/chat').call_failed) 'binary bytes are rejected as text without crashing the turn or producing an invalid JSON receipt'
 }
 $read.calls[0].args.path='short.txt';Plan @($read,$textReply);$null=Message;$textResult=Feedback;Check ($textResult.ok-and$textResult.result.content-eq'静夜里留一小段。'-and$textResult.result.source_mount-eq'materials'-and$textResult.result.source_path-eq'short.txt') 'UTF-8 text receipt retains the actual workspace source and content'
 $listing=@{version=1;kind='call';text=$null;calls=@(@{id='list-materials';tool='workspace.list';args=@{mount='materials'}})};Plan @($listing,$textReply);$null=Message;$listed=Feedback
 Check ($listed.ok-and$listed.result.source_mount-eq'materials'-and$listed.result.source_path-eq''-and$listed.result.entries-contains'short.txt') 'directory observation keeps its source mount and relative path'
 Plan @((Calls 'picture.bin'),(Reply));$trace=Message;$feedback=Feedback
 Check ($feedback.ok-and$feedback.result.image_attached-and$feedback.result.vision_available-and!$feedback.result.image) 'actor receives an actual image content part, not base64 text'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json
 Check ($capture.messages[-1].content[1].image_url.url.StartsWith('data:image/jpeg;base64,')) 'authorized bytes are reencoded as bounded JPEG'
 $initialPerception=($capture.messages[1].content|ConvertFrom-Json).perception;$delivered=($capture.messages[-1].content[0].text|ConvertFrom-Json).perception
 Check ($initialPerception.image_input_available-and$initialPerception.images_in_current_request-eq0-and!$initialPerception.continuous_screen_feed-and$delivered.images_in_current_request-eq1) 'connection capability and actual delivered image are distinct authoritative context'
 $review=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json
 Check ($review.messages[-1].content[1].image_url.url-eq$capture.messages[-1].content[1].image_url.url) 'reviewer receives the same newest picture'
 Check (($review.messages[-1].content[0].text|ConvertFrom-Json).perception.images_in_current_request-eq1) 'review receives the current image delivery count rather than inferring a continuous feed'
 Check ($trace.visual_inputs[0].source_id-eq$feedback.source_id-and$trace.visual_inputs[0].attached-and$trace.visual_inputs[0].delivery_state-eq'acknowledged') 'image source receipt records an accepted model request without pixel data in trace'
 $archive=Api '/api/archive' @{id=$feedback.source_id}
 $pictureSource=$feedback.source_id
 Check ($archive.records[0].data.result.source_path-eq'picture.bin'-and$archive.records[0].data.result.image.base64) 'encrypted archive retains original observation provenance'
 $originalPixels=$capture.messages[-1].content[1].image_url.url
 [IO.File]::WriteAllText((Join-Path $cfg.dataRoot materials/picture.bin),'the original workspace file has changed')
 $revisit=@{version=1;kind='call';text=$null;calls=@(@{id='revisit';tool='observation.revisit';args=@{source_id=$pictureSource}})};Plan @($revisit,(Reply));$trace=Message;$again=Feedback;$seen=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60
 Check ($again.ok-and$again.source_id-eq$pictureSource-and$again.result.original_tool-eq'workspace.view_image'-and$again.result.context_role-eq'revisited_past_observation'-and$seen.messages[-1].content[1].image_url.url-eq$originalPixels) 'known authorized source replays original picture even when the workspace file changed'
 $unchanged=Api '/api/archive' @{id=$pictureSource};Check ($unchanged.records[0].data.result.PSObject.Properties.Name-notcontains'revisited_at_ms') 'revisiting never edits the original observation or invents a fresh capture'
 $remember=@{version=1;kind='call';text=$null;calls=@(@{id='source-reference';tool='memory.remember';args=@{kind='inference';text='这张图是当时实际提供过的素材。';source_ids=@($pictureSource)}})};Plan @($remember,(Reply));$null=Message;$memoryId=(Feedback).result.id;Check ($memoryId) 'recalled source can retain an authorized memory reference'
 $null=Api '/api/access' @{id=$memoryId;accessible=$false};Plan @($revisit,(Reply));$trace=Message;Check ((Feedback).error.code-eq'observation_not_accessible'-and$trace.visual_inputs.Count-eq0) 'hidden observation source cannot be recalled into actual image parts'
 $null=Api '/api/access' @{id=$memoryId;accessible=$true};Plan @($revisit,(Reply));$null=Message;Check ((Feedback).ok-and(Feedback).result.image_attached) 'restored source permission enables an explicit new revisit'
 $forged=@{version=1;kind='call';text=$null;calls=@(@{id='unknown-source';tool='observation.revisit';args=@{source_id='not-authorized-by-context'}})};Plan @($forged,(Reply));$null=Message;Check ((Feedback).error.code-eq'observation_not_accessible') 'revisit cannot enumerate arbitrary archived observations'
 $internal=@{version=1;kind='call';text=$null;calls=@(@{id='private-state';tool='self.observe';args=@{}})};$privateRead=@{version=1;kind='call';text=$null;calls=@(@{id='private-revisit';tool='observation.revisit';args=@{source_id='${last_source_id}'}})};Plan @($internal,$privateRead,(Reply));$null=Message;Check ((Feedback).error.code-eq'observation_not_revisitable') 'concrete observation recall cannot expose old internal personal-state snapshots'
 $wrapped='```json'+"`n"+((Reply)|ConvertTo-Json -Compress)+"`n"+'```'
 Plan @($wrapped);$trace=Message
 Check ($trace.turns.Count-eq1-and$trace.turns[0].envelope_normalized) 'a complete fenced JSON envelope is normalized without another model call'
 foreach($prefix in @(('```JSON'+"`r`n"),('```json'+"`r`n"),('```'+"`r`n"))){
  $candidate=[Text.Encoding]::UTF8.GetString([byte[]](239,187,191))+" `r`n"+$prefix+((Reply)|ConvertTo-Json -Compress)+"`r`n"+'```'
  Plan @($candidate);$trace=Message;Check ($trace.turns.Count-eq1-and$trace.turns[0].envelope_normalized) 'complete BOM or CRLF JSON wrappers do not need a paid repair'
 }
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json
 $context=$capture.messages[1].content|ConvertFrom-Json;$past=@($context.authorized_context|Where-Object id -eq $pictureSource)
 Check ($past.Count-eq1-and$past[0].data.result.image_delivered_to_model-and!$past[0].data.result.image) 'a following turn retains a real image-delivery receipt without replaying pixels'
 Plan @('看到了，左边是红色方块，右边是蓝色圆。');$trace=Message
 Check ($trace.turns.Count-eq1-and$trace.turns[0].plain_reply_wrapped-and$trace.reviews.Count-eq1) 'plain dialogue can become a reply-only candidate and still requires the final reviewer'
 Plan @(('explanation '+((Reply)|ConvertTo-Json -Compress)),(Reply));$trace=Message
 Check ($trace.turns.Count-eq2-and!$trace.turns[0].envelope_normalized-and$trace.turns[0].status-eq10) 'prose surrounding JSON still requires a bounded repair rather than substring execution'
 Plan @((Calls 'invalid.png'),(Reply));$null=Message;Check ((Feedback).error.code-eq'image_decode_failed') 'fake image is rejected before model attachment'
 Plan @((Calls '../runtime/memory-key.dpapi'),(Reply));$null=Message;Check ((Feedback).error.code-eq'image_workspace_read_denied') 'image tool cannot cross workspace authority'
 Copy-Item -LiteralPath $png.FullName -Destination (Join-Path $cfg.dataRoot materials/quiet.png)
 $messageCount=@((Api '/api/chat').messages|Where-Object role -eq assistant).Count
 Plan @((Calls 'quiet.png'),@{version=1;kind='silence';text=$null;calls=@()});$quietTrace=Message;$quietSource=$quietTrace.tool_results[0].source_id
 Check ($quietTrace.silent-and$quietSource-and@((Api '/api/chat').messages|Where-Object role -eq assistant).Count-eq$messageCount) 'successful image observation may finish silently without an empty chat bubble'
 Plan @((Reply));$null=Message;$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;$context=$capture.messages[1].content|ConvertFrom-Json;$quietReceipt=@($context.authorized_context|Where-Object id -eq $quietSource)
 Check ($quietReceipt.Count-eq1-and$quietReceipt[0].data.result.source_path-eq'quiet.png'-and$quietReceipt[0].data.result.image_delivered_to_model-and!$quietReceipt[0].data.result.image) 'next conversation retains a silent real observation and its actual image delivery receipt without pixels'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'silent observation host stops cleanly';Remove-Item -LiteralPath $ready
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath $ready} 'silent observation restart';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 Plan @((Reply));$null=Message;$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;$context=$capture.messages[1].content|ConvertFrom-Json
 Check (@($context.authorized_context|Where-Object id -eq $quietSource).Count-eq1) 'silent observation continuity survives a normal restart without replaying the tool'
 $remember.calls[0].args.source_ids=@($quietSource);Plan @($remember,(Reply));$null=Message;$quietMemory=(Feedback).result.id;$null=Api '/api/access' @{id=$quietMemory;accessible=$false}
 Plan @((Reply));$null=Message;$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;$context=$capture.messages[1].content|ConvertFrom-Json
 Check (@($context.authorized_context|Where-Object id -eq $quietSource).Count-eq0) 'independent observation index respects withdrawn source access after restart'
 # A read-only capture no longer consumes a separate semantic-review call.
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 50|ConvertFrom-Json -AsHashtable;$settings.maxStartsPerHour=(Api '/api/dashboard').model_budget.used+1;$null=Api '/api/settings' @{settings=$settings}
 Plan @((Calls 'quiet.png'),(Reply));$blocked=Message;$blockedSource=$blocked.tool_results[0].source_id
 Check ((Api '/api/chat').call_error.code-eq'local_hourly_budget'-and$blockedSource-and$blocked.visual_inputs[0].delivery_state-eq'prepared') 'budget exhausted after capture leaves a prepared image rather than a false delivered receipt'
 $settings.maxStartsPerHour=600;$null=Api '/api/settings' @{settings=$settings};Plan @((Reply));$null=Message;$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;$context=$capture.messages[1].content|ConvertFrom-Json;$prepared=@($context.authorized_context|Where-Object id -eq $blockedSource)
 Check ($prepared.Count-eq1-and$prepared[0].data.result.image_delivery_status-eq'prepared_only'-and!$prepared[0].data.result.image_delivered_to_model) 'later context distinguishes saved pixels from a request that was never made'
 $originalBlocked=Api '/api/archive' @{id=$blockedSource};$silentRevisit=@{version=1;kind='call';text=$null;calls=@(@{id='silent-revisit';tool='observation.revisit';args=@{source_id=$blockedSource}})}
 Plan @($silentRevisit,@{version=1;kind='silence';text=$null;calls=@()});$recalled=Message;Check ($recalled.silent-and$recalled.visual_inputs[0].delivery_state-eq'acknowledged') 'later successful original-image delivery may end silently'
 Plan @((Reply));$null=Message;$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;$context=$capture.messages[1].content|ConvertFrom-Json;$delivered=@($context.authorized_context|Where-Object id -eq $blockedSource)
 Check ($delivered.Count-eq1-and$delivered[0].data.result.image_delivery_status-eq'acknowledged'-and$delivered[0].data.result.image_delivered_to_model) 'a silent successful revisit upgrades delivery evidence without depending on a spoken reply'
 $stillOriginal=Api '/api/archive' @{id=$blockedSource};Check (($stillOriginal|ConvertTo-Json -Depth 50 -Compress)-eq($originalBlocked|ConvertTo-Json -Depth 50 -Compress)) 'delivery ledger and renewed relevance never rewrite the original observation'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 50|ConvertFrom-Json -AsHashtable;$settings.profiles[1].supportsVision=$false;$null=Api '/api/settings' @{settings=$settings}
 Plan @($revisit,(Reply));$null=Message;Check ((Feedback).error.code-in@('vision_not_enabled','observation_not_accessible')) 'text-only profile never receives archived pixel bytes'
 Plan @((Calls 'picture.bin'),(Reply));$null=Message;Check ((Feedback).error.code-eq'vision_not_enabled') 'disabled vision does not read or attach image bytes'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'vision fixture exits cleanly'
 @{checks=$checks;completed=$true;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Vision passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
