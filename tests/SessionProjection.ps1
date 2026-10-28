param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('session-projection-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='session-projection-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$until=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$until){if(&$Probe){return};Start-Sleep -Milliseconds 45};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer session-projection-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 75 -Compress))};Invoke-RestMethod @p}
function Native($Id,$Name,$Arguments){@{fixture_native_calls=@(@{id=$Id;name=$Name;arguments=$Arguments})}}
function Plan($Items,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$out=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$out.trace_id}}
function Start-Backend{$script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'Projection fixture exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.recentMessageCount=2
 $cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.contextControl=@{enabled=$true;preset='balanced';sessionProjection=$true;sessionMaxTurns=4;sessionBytes=65536}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Projection actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-actor';supportsToolCalls=$true;supportsJsonObject=$true},@{id='second';label='Other model';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-other';supportsToolCalls=$true;supportsJsonObject=$true},@{id='review';label='Reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $root config.json) -Encoding utf8
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true;Start-Backend;$null=Api '/api/model' @{id='actor'}
 Plan @((Native 'one' 'ha_turn_reply' @{text='one approved'}));$first=Message 'PROJECTION_PRIVATE_FIRST_EVENT'
 $firstWire=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90
 Check ($first.session_projection.mode-eq'baseline') 'first turn establishes a bounded baseline'
 Plan @((Native 'remember' 'ha_memory_DOT_remember' @{kind='conversation';text='PROJECTION_PRIVATE_FIRST_EVENT';source_ids=@($first.source_event_id)}),(Native 'two' 'ha_turn_reply' @{text='two approved'}));$second=Message 'keep the first detail'
 $secondWire=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90
 Check ($second.session_projection.mode-eq'append') 'next turn appends after the prior approved turn'
 Check (($firstWire.messages[0..1]|ConvertTo-Json -Depth 85 -Compress)-ceq($secondWire.messages[0..1]|ConvertTo-Json -Depth 85 -Compress)) 'previously sent provider messages remain byte equivalent across turns'
 $reviewWire=Get-Content (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 90;$review=($reviewWire.messages[1].content|ConvertFrom-Json -Depth 85).event_context|ConvertFrom-Json -Depth 85
 Check ($review.input-eq'keep the first detail'-and$review.event_id-eq$second.source_event_id-and!$review.projection) 'reviewer receives the materialized current event instead of an unresolved delta or old input'
 Stop-Backend;Start-Backend
 Plan @((Native 'three' 'ha_turn_reply' @{text='three approved'}));$third=Message 'a third turn'
 Check ($third.session_projection.mode-eq'append') 'encrypted projection survives a backend restart'
 $thirdWire=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90
 Check (($secondWire.messages|ConvertTo-Json -Depth 85 -Compress)-ceq($thirdWire.messages[0..($secondWire.messages.Count-1)]|ConvertTo-Json -Depth 85 -Compress)) 'restart restores the exact retained model message prefix'
 Plan @((Native 'four' 'ha_turn_reply' @{text='four approved'}));$fourth=Message 'a fourth turn';Check ($fourth.session_projection.mode-eq'append') 'capacity permits another append without rolling the first messages away'
 Plan @((Native 'five' 'ha_turn_reply' @{text='five approved'}));$fifth=Message 'a fifth turn'
 Check ($fifth.session_projection.mode-eq'baseline'-and$fifth.session_projection.reason-eq'turn_boundary') 'configured segment boundary rebases from current retrieval rather than growing without limit'
 $memory=(Api '/api/memories').memories|Where-Object text -eq 'PROJECTION_PRIVATE_FIRST_EVENT'|Select-Object -First 1
 Check ($null-ne$memory) 'source-backed test memory exists'
 $null=Api '/api/access' @{id=$memory.id;accessible=$false}
 Plan @((Native 'six' 'ha_turn_reply' @{text='six approved'}));$sixth=Message 'continue without the restricted detail'
 $sixthWire=Get-Content (Join-Path $providerRoot last-request.json) -Raw
 Check ($sixth.session_projection.mode-eq'baseline'-and$sixth.session_projection.reason-eq'authority_changed') 'revocation invalidates the retained prefix immediately'
 Check (!$sixthWire.Contains('PROJECTION_PRIVATE_FIRST_EVENT')) 'revoked raw content and old tool arguments are absent from the new provider request'
 $null=Api '/api/dialogue/rebuild' @{}
 Plan @((Native 'seven' 'ha_turn_reply' @{text='seven approved'}));$seventh=Message 'fresh dialogue view'
 Check ($seventh.session_projection.mode-eq'baseline'-and$seventh.session_projection.reason-eq'authority_changed') 'manual dialogue rebuild discards the previous projection'
 $null=Api '/api/model' @{id='second'};Plan @((Native 'eight' 'ha_turn_reply' @{text='eight approved'}));$eighth=Message 'another configured model'
 Check ($eighth.session_projection.mode-eq'baseline'-and$eighth.session_projection.reason-eq'new_session') 'different model connection starts its own projection'
 $settings=(Api '/api/dashboard').settings;$settings.contextControl.sessionProjection=$false;$null=Api '/api/settings' @{settings=$settings}
 Plan @((Native 'nine' 'ha_turn_reply' @{text='nine approved'}));$ninth=Message 'projection disabled'
 $disabled=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 85
 Check (!$ninth.session_projection-and$disabled.messages.Count-eq2) 'configuration opt-out restores ordinary context assembly'
 $original=Api '/api/archive' @{id=$first.source_event_id};Check ($original.records[0].data.text-eq'PROJECTION_PRIVATE_FIRST_EVENT') 'rebasing and revocation retain the immutable original archive'
 $settings=(Api '/api/dashboard').settings;$settings.contextControl.sessionProjection=$true;$null=Api '/api/settings' @{settings=$settings};$null=Api '/api/access' @{id=$memory.id;accessible=$true}
 Plan @((Native 'revise-now' 'ha_memory_DOT_revise' @{id=$memory.id;expected_revision=$memory.revision;kind='summary';text='revised synthetic detail';source_ids=@('${event_id}')}),(Native 'revise-finished' 'ha_turn_reply' @{text='revision completed'}));$revised=Message 'revise the stored detail'
 Check (!$revised.call_error-and((Api '/api/memories').memories|Where-Object id -eq $memory.id).revision-eq$memory.revision+1) 'approved in-turn revision continues using its explicit receipt without accepting unrelated source changes'
 $beforeCalls=@(Get-Content (Join-Path $providerRoot requests.jsonl)).Count
 Plan @((Native 'forget-now' 'ha_memory_DOT_forget' @{id=$memory.id}));$interrupted=Message 'forget that stored detail now'
 $afterCalls=@(Get-Content (Join-Path $providerRoot requests.jsonl)).Count
 Check ($interrupted.call_error.code-eq'context_authorization_changed'-and$afterCalls-eq$beforeCalls+1) 'mid-turn revocation stops before another model request or proposal uses the old prefix'
 Check (!((Api '/api/memories').memories|Where-Object id -eq $memory.id).accessible) 'successful revocation is retained when the remaining turn is stopped'
 Stop-Backend
 $plainFound=$false;foreach($file in Get-ChildItem (Join-Path $cfg.dataRoot archive) -File -Recurse){if([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName)).Contains('PROJECTION_PRIVATE_FIRST_EVENT')){$plainFound=$true}}
 Check (!$plainFound) 'private archive and session projection contents remain encrypted on disk'
 @{completed=$true;checks=$checks;root=$root;actual_paid_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Session projection passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
