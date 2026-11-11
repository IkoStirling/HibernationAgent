param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$MentorExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot,[string]$PluginRoot='', [switch]$Live)
$ErrorActionPreference='Stop';if($Live){throw 'This suite uses fixtures only; live model calls are not part of validation.'};$checks=0;$service=$null;$provider=$null
$root=Join-Path $ScratchRoot ('mentor-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='mentor-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(90);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer mentor-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Run-Mentor($Goal,$Name='mentor.ask',$Wait=$true){
 $callId='mentor-'+[Guid]::NewGuid().ToString('N').Substring(0,16);$arguments=@{goal=$Goal}
 $responses=@(@{response=(Calls @((Tool $callId $Name $arguments)))},@{response=@{version=1;kind='reply';text='我和导师聊了几句。';calls=@()}})
 @{nonce=[Guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='独立导师会话验证'}
 if(!$Wait){return $callId};Await {!(Api '/api/chat').busy} 'mentor turn'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60
 foreach($message in $capture.messages){if($message.role-eq'user'){try{$data=$message.content|ConvertFrom-Json -Depth 55;foreach($receipt in $data.tool_results){if($receipt.call_id-eq$callId){return $receipt}}}catch{}}};throw 'mentor receipt missing'
}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.Remove('backgroundBudget') # Mentor compatibility does not depend on the newer daily-budget feature.
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=if($PluginRoot){$PluginRoot}else{Join-Path (Split-Path $Executable) plugins}
 $cfg.profiles=@($cfg.profiles[0],@{id='mentor-fixture';label='Mentor fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.plugins=@{mentor=@{enabled=$true;backend='chatgpt';codexExecutable=$MentorExecutable;pythonExecutable=(Join-Path $ProjectRoot 'out/deps/style-lora-venv/Scripts/python.exe');cooldownSeconds=0;timeoutSeconds=10;maxRequestsPerHour=30}}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Mentor backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+(Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port
 Check ((Api '/api/dashboard').health-eq'ok') 'mentor startup leaves the main runtime healthy'
 $initial=@((Api '/api/capabilities').plugins|Where-Object id -eq mentor)[0].current;Check ($initial.requests_used-eq0-and$initial.execution_enabled) 'default enabled mentor performs no model call at startup'
 $null=Api '/api/model' @{id='mentor-fixture'}
 $status=(Api '/api/mentor/session').result;$logical=$status.session_id
 Check ($logical-and$status.encrypted-and$status.requests_used-eq0) 'metadata read initializes encrypted canonical session without model usage'
 Check ($status.permission_policy-eq'codex_user_configuration') 'mentor inherits its own permission policy'
 $conversation=Run-Mentor 'fixture_chat'
 Check ($conversation.ok-and$conversation.source_id-and$conversation.result.summary-eq'可以聊一会儿。你刚才想到什么有趣的事？') 'casual conversation works without selected operational tools'
 Check (!$conversation.result.host_execution_required-and$conversation.result.execution_mode-eq'native_mentor') 'native mentor does not request host operation replay'
 Check ((Api '/api/chat').messages[-1].text-eq'我和导师聊了几句。') 'main character keeps final dialogue expression'
 $continuity=Run-Mentor 'fixture_continuity'
 Check ($continuity.ok-and$continuity.result.summary-eq'native-turn-2') 'second request reuses same native conversation'
 $memory=Run-Mentor 'fixture_memory'
 Check ($memory.ok-and(Api '/api/mentor/session').result.memory_count-eq1) 'mentor saves sourced independent memory through encrypted tool'
 $delegated=Run-Mentor 'fixture_execute_write' 'mentor.execute'
 Check ($delegated.ok-and$delegated.result.operations_executed-and$delegated.result.receipts.Count-eq1) 'native execution returns actual operation receipt'
 $proof=Join-Path $cfg.dataRoot 'mentors/default/workspace/mentor-proof.txt'
 Check ((Get-Content -LiteralPath $proof -Raw)-eq'mentor executed this local task') 'mentor task product belongs to dedicated workspace'
 Check (!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot 'drafts/mentor-proof.txt'))) 'host does not double execute native work in character workspace'
 $null=Api '/api/mentor/session/control' @{action='rebuild'}
 Check ((Api '/api/mentor/session').result.session_id-eq$logical) 'context rebuild preserves canonical logical session'
 $recovered=Run-Mentor 'fixture_recovered'
 Check ($recovered.ok-and$recovered.result.summary-eq'recovered-history') 'new native context receives preserved original conversation'
 Check (@((Api '/api/capabilities').tools|Where-Object tool -like 'mentor.forward*').Count-eq0) 'internal mentor relay cannot bypass owner-bound development tool'
 $forwardId='development-'+[Guid]::NewGuid().ToString('N').Substring(0,12)
 $responses=@(@{response=(Calls @((Tool $forwardId 'development.request' @{title='合成转发验证';details='这是独立验证数据，请只接收和分析，不修改代码。'})))},@{response=@{version=1;kind='reply';text='需求已按实际回执处理。';calls=@()}})
 @{nonce=[Guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='请把这个合成开发需求转交导师。'};Await {!(Api '/api/chat').busy} 'development relay'
 $request=@((Api '/api/development-requests').items)[-1]
 Check ($request.developer_notified-and$request.forward_receipt.provider_thread_id-and$request.forward_receipt.turn_id) 'development relay has actual provider acceptance rather than only local inbox entry'
 Check ($request.target-eq'mentor'-and!$request.forward_receipt.automatic_execution-and$request.forward_receipt.session_id-eq$logical) 'development relay reuses same logical mentor without authorizing code execution'
 Await {!(Api '/api/mentor/session').result.busy} 'development mentor completion'
 $null=Run-Mentor 'fixture_timeout' 'mentor.ask' $false
 Await {(Api '/api/mentor/session').result.busy} 'busy mentor status'
 $cancel=Api '/api/mentor/session/control' @{action='cancel'}
 Check ($cancel.ok) 'cancel remains available while character is busy'
 Await {!(Api '/api/chat').busy} 'cancelled character turn'
 $status=(Api '/api/mentor/session').result
 Check (!$status.busy-and$status.session_id-eq$logical) 'cancel releases session without deleting memory'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.plugins.mentor.allowExecution=$false;$null=Api '/api/settings' @{settings=$settings}
 Check (@((Api '/api/capabilities').tools|Where-Object tool -eq mentor.execute).Count-eq0) 'operator may turn off task delegation'
 $null=Api '/api/shutdown' @{};Wait-Process -Id $service.Id -Timeout 10
 Remove-Item -LiteralPath $ready
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'restart failed'};Test-Path -LiteralPath $ready} 'restarted backend';$script:origin='http://127.0.0.1:'+(Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port
 $status=(Api '/api/mentor/session').result
 Check ($status.session_id-eq$logical-and$status.memory_count-eq1) 'backend restart preserves same mentor and private memories'
 foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $cfg.dataRoot 'mentors/default') -Filter '*.dpapi' -Recurse)){
  $rawMentor=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName));Check (!$rawMentor.Contains('fixture-private-memory')-and!$rawMentor.Contains('fixture_chat')) 'mentor records remain encrypted on disk'
 }
 $null=Api '/api/shutdown' @{};Wait-Process -Id $service.Id -Timeout 10
 $raw=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)))
 Check (!$raw.Contains('我和导师聊了几句。')-and!$raw.Contains('不知道怎样查找授权素材中的文件')) 'consultations and final dialogue remain encrypted in the application archive'
 @{completed=$true;checks=$checks;live=[bool]$Live;production_archive_read=$false;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Mentor passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
