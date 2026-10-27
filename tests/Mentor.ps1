param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$MentorExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot,[switch]$Live)
$ErrorActionPreference='Stop';$checks=0;$service=$null;$provider=$null
$root=Join-Path $ScratchRoot ('mentor-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='mentor-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(90);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer mentor-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Run-Turn($Goal){
 $ask=Calls @((Tool mentor mentor.ask @{goal=$Goal;obstacle='不知道怎样查找授权素材中的文件。';tool_names=@('workspace.list')}))
 $list=Calls @((Tool execute workspace.list @{mount='materials'}));$reply=@{version=1;kind='reply';text='我先看看素材区里有什么。';calls=@()}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$ask},@{response=$list},@{response=$reply})}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='隔离的操作导师验证'};Await {!(Api '/api/chat').busy} 'mentor consultation'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60
 $receipts=@();foreach($message in $capture.messages){if($message.role-eq'user'){try{$data=$message.content|ConvertFrom-Json -Depth 55;if($data.tool_results){$receipts+=@($data.tool_results)}}catch{}}};return $receipts
}
function Run-Delegate($Goal,$Names,$Repeat=$false){
 $arguments=@{goal=$Goal;obstacle='这项操作交给导师在本机执行，返回真实结果。';tool_names=@($Names)}
 $responses=@(@{response=(Calls @((Tool delegate mentor.execute $arguments)))})
 if($Repeat){$responses+=@{response=(Calls @((Tool delegate2 mentor.execute $arguments)))}}
 $responses+=@{response=@{version=1;kind='reply';text='我把这件事处理了一下。';calls=@()}}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=$responses}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='独立存档的导师执行验证'};Await {!(Api '/api/chat').busy} 'mentor delegated execution'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60
 $receipts=@();foreach($message in $capture.messages){if($message.role-eq'user'){try{$data=$message.content|ConvertFrom-Json -Depth 55;if($data.tool_results){$receipts+=@($data.tool_results)}}catch{}}};return $receipts
}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0],@{id='mentor-fixture';label='Mentor fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.plugins=@{mentor=@{enabled=$true;backend='chatgpt';codexExecutable=$MentorExecutable;cooldownSeconds=0;timeoutSeconds=60;maxRequestsPerHour=12}}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Mentor backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+(Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port
 Check ((Api '/api/dashboard').health-eq'ok') 'mentor startup leaves the main runtime healthy'
 $initial=@((Api '/api/capabilities').plugins|Where-Object id -eq mentor)[0].current;Check ($initial.requests_used-eq0-and$initial.execution_enabled) 'default enabled mentor performs no model call at startup'
 $null=Api '/api/model' @{id='mentor-fixture'}
 $receipts=Run-Turn '查找素材';$advice=@($receipts|Where-Object call_id -eq mentor)[0];$execution=@($receipts|Where-Object call_id -eq execute)[0]
 Check ($advice.ok-and$advice.source_id-and$advice.result.advisory_only-and!$advice.result.operations_executed) 'mentor advice is sourced and explicitly not execution'
 Check ($execution.ok-and$execution.source_id) 'character independently executes through the existing workspace permission path'
 $chat=Api '/api/chat';Check ($chat.messages[-1].text-eq'我先看看素材区里有什么。'-and$chat.messages.Count-eq2) 'only the character final reply appears in chat'
 $record=Api '/api/archive' @{id=$advice.source_id};Check ($record.records[0].data.tool-eq'mentor.ask'-and!$record.records[0].data.result.operations_executed) 'encrypted trace preserves advice provenance rather than completed work'
 $goal=if($Live){'请直接通过workspace.write在草稿根创建mentor-proof.txt，内容精确为mentor executed this local task，然后通过workspace.read读取核对。不要把这两步只留作建议。'}else{'fixture_execute_write'}
 $delegation=Run-Delegate $goal @('workspace.write','workspace.read') (!$Live);$done=@($delegation|Where-Object call_id -eq delegate)[0]
 Check ($done.ok-and$done.result.delegated-and!$done.result.advisory_only-and$done.result.operations_executed-and$done.result.all_steps_succeeded) 'mentor request directly executes local operations instead of merely giving advice'
 Check ((Get-Content -LiteralPath (Join-Path $cfg.dataRoot drafts/mentor-proof.txt) -Raw)-eq'mentor executed this local task') 'file is actually written on this computer by the delegated operation'
 Check ($done.result.receipts.Count-eq2-and$done.result.receipts[0].receipt.result.written-and$done.result.receipts[1].receipt.result.content-eq'mentor executed this local task') 'write and read each carry their authentic execution receipts'
 Check ((Api '/api/chat').messages[-1].text-eq'我把这件事处理了一下。') 'mentor execution leaves final character expression to the main connection'
 if(!$Live){
  $replayed=@($delegation|Where-Object call_id -eq delegate2)[0];Check ($replayed.ok-and$replayed.result.receipts[0].receipt.source_id-eq$done.result.receipts[0].receipt.source_id) 'repeated delegate request reuses saved execution without repeating side effects'
  $partial=@(Run-Delegate 'fixture_execute_partial' @('workspace.write')|Where-Object call_id -eq delegate)[0]
  Check ($partial.ok-and$partial.result.operations_executed-and!$partial.result.all_steps_succeeded-and$partial.result.steps_succeeded-eq1-and$partial.result.receipts.Count-eq2) 'partial success remains explicit when a later operation is denied'
  Check ((Test-Path -LiteralPath (Join-Path $cfg.dataRoot drafts/mentor-before-denial.txt))-and!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot drafts/mentor-after-denial.txt))-and!(Test-Path -LiteralPath (Join-Path $cfg.dataRoot archive/forbidden.txt))) 'workspace traversal is rejected and following steps do not run'
  $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.plugins.mentor.allowExecution=$false;$null=Api '/api/settings' @{settings=$settings}
  Check (@((Api '/api/capabilities').tools|Where-Object tool -eq mentor.execute).Count-eq0-and@((Api '/api/capabilities').tools|Where-Object tool -eq mentor.ask).Count-eq1) 'operator can disable direct execution while retaining advice'
  $settings.plugins.mentor.allowExecution=$true;$null=Api '/api/settings' @{settings=$settings}
 }
 if(!$Live){
  $receipts=Run-Turn 'fixture_forbidden';$bad=@($receipts|Where-Object call_id -eq mentor)[0];Check (!$bad.ok-and$bad.error.code-eq'mentor_invalid_advice') 'unknown tool suggestions fail closed without taking over the turn'
  $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.plugins.mentor.backend='none';$null=Api '/api/settings' @{settings=$settings}
  $receipts=Run-Turn '查找素材';$bad=@($receipts|Where-Object call_id -eq mentor)[0];Check (!$bad.ok-and$bad.error.code-eq'mentor_not_configured'-and(Api '/api/chat').messages[-1].text-eq'我先看看素材区里有什么。') 'unconfigured mentor returns a normal receipt and main character can continue'
  $settings.plugins.mentor.enabled=$false;$null=Api '/api/settings' @{settings=$settings};Check (@((Api '/api/capabilities').tools|Where-Object {$_.tool-like'mentor.*'}).Count-eq0) 'disabling mentor removes its tools without changing the main model'
 }
 $null=Api '/api/shutdown' @{};Wait-Process -Id $service.Id -Timeout 10
 $raw=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)))
 Check (!$raw.Contains('我先看看素材区里有什么。')-and!$raw.Contains('不知道怎样查找授权素材中的文件')) 'consultations and final dialogue remain encrypted in the application archive'
 @{completed=$true;checks=$checks;live=[bool]$Live;production_archive_read=$false;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Mentor passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
