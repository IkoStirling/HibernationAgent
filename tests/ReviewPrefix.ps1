param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('review-prefix-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='review-prefix-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Label){$until=[DateTime]::UtcNow.AddSeconds(35);while([DateTime]::UtcNow-lt$until){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Label"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer review-prefix-fixture'};TimeoutSec=15};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 85 -Compress))};Invoke-RestMethod @p}
function Plan($Items,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 70|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@();presentation=@{speech=$false;bubble=$false};carry_forward=$null;appraisal=$null}}
function Message($Text){$message=@{request_id=[Guid]::NewGuid().ToString('N');text=$Text};$null=Api '/api/message' $message;Await {!(Api '/api/chat/status').busy} 'turn';$saved=Api '/api/message' $message;if(!$saved.duplicate-or!$saved.request.trace_id){throw 'Trace missing'};Api '/api/trace' @{id=$saved.request.trace_id}}
function ReviewWire{Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 90}
function Start-Backend{$script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'Backend exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(12000)) 'clean backend shutdown'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null;$provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.maxStartsPerHour=1000;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;supportsStreaming=$false},@{id='review';label='Review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true;maxOutputTokens=8192})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg.contextControl=@{reviewPolicy='layered';reviewLayout='prefix';reviewReasoningEffort='none'};$cfg|ConvertTo-Json -Depth 80|Set-Content -LiteralPath (Join-Path $root config.json) -Encoding utf8
 $approve=@{decision='approve';reason='ok';guidance='AUDIT_VERDICT_NOT_HISTORY'};Plan @($approve) $true;Start-Backend;$null=Api '/api/model' @{id='actor'}
 Plan @(@{version=1;kind='call';text=$null;calls=@(@{id='remember';tool='memory.remember';args=@{kind='conversation';text='PRIVATE_PREFIX_MEMORY';source_ids=@('${event_id}')}})},(Reply 'FIRST_APPROVED'))
 $first=Message '这一轮建立有来源的测试记忆。';Check (!(Api '/api/chat/status').call_failed) 'first independent review succeeds';$firstWire=ReviewWire
 Plan @((Reply 'SECOND_APPROVED'));$second=Message '接着聊一件很普通的小事。';$secondWire=ReviewWire
 Check ($second.reviews[-1].input_layout-eq'review_prefix_v1'-and$second.reviews[-1].context_projection.reused) 'next user turn reuses reviewer definitions independently of actor'
 $prefixCount=$firstWire.messages.Count-1;Check (($firstWire.messages[0..($prefixCount-1)]|ConvertTo-Json -Depth 90 -Compress)-ceq($secondWire.messages[0..($prefixCount-1)]|ConvertTo-Json -Depth 90 -Compress)) 'sent definition prefix remains byte equivalent across user turns'
 $wireText=$secondWire|ConvertTo-Json -Depth 90 -Compress
 Check (!$wireText.Contains('AUDIT_VERDICT_NOT_HISTORY')) 'old auditor verdict is never model history'
 $snapshots=@($secondWire.messages|Where-Object {$_.content.Contains('"review_snapshot"')});Check ($snapshots.Count-eq1-and($snapshots[0].content|ConvertFrom-Json -Depth 90).review_snapshot.data.candidate.text-eq'SECOND_APPROVED') 'only current candidate and one complete current snapshot are sent'
 Check ($wireText.Contains('PRIVATE_PREFIX_MEMORY')) 'authorized source evidence actually reaches reviewer'
 Stop-Backend;Start-Backend;Plan @((Reply 'AFTER_RESTART'));$restarted=Message '重启后继续。';Check ($restarted.reviews[-1].context_projection.reused) 'encrypted definition prefix survives normal process restart'
 $memory=@((Api '/api/memories').memories|Where-Object text -eq 'PRIVATE_PREFIX_MEMORY')[0];Check ($null-ne$memory) 'revocation fixture has a real source-backed memory';$null=Api '/api/access' @{id=$memory.id;accessible=$false}
 Plan @((Reply 'AFTER_REVOKE'));$revoked=Message '现在不再使用受限资料。';$revokedWire=ReviewWire|ConvertTo-Json -Depth 90 -Compress
 Check (!$revoked.reviews[-1].context_projection.reused-and!$revokedWire.Contains('PRIVATE_PREFIX_MEMORY')) 'revocation clears retained source definitions before next review'
 $settings=(Api '/api/dashboard').settings;$settings.contextControl|Add-Member -NotePropertyName reviewAttempts -NotePropertyValue 1 -Force;$null=Api '/api/settings' @{settings=$settings};Plan @((Reply 'REVIEW_SETTING_ONLY'));$independent=Message '只改审核选项后继续普通交谈。'
 Check ($independent.session_projection.mode-eq'append') 'review-only setting changes preserve ordinary actor message prefix'
 Plan @(@{decision='deny';reason='tool_intent';guidance='拒绝本次候选。'}) $true;Plan @((Reply 'UNAPPROVED_MUST_NOT_DISPLAY'));$denied=Message '验证审核仍然生效。'
 Check ((Api '/api/chat/status').call_failed-and@((Api '/api/chat').messages|Where-Object text -eq 'UNAPPROVED_MUST_NOT_DISPLAY').Count-eq0) 'independent denial still prevents publication'
 Stop-Backend;@{passed=$true;checks=$checks;paid_requests=0;synthetic_only=$true}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8;Write-Output "$checks review prefix checks passed"
}finally{if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(10000)}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id -Force}};if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id -Force}}
