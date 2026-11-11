param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot,[string]$ScratchRoot='E:/Aliyat/Validation/ActivityFollowup')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ('followup-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='activity-followup-synthetic';$checks=[Collections.Generic.List[string]]::new();$service=$null;$provider=$null;$desktop=$null
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$checks.Add($Name);Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Label,[int]$Seconds=30){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if($script:service-and$script:service.HasExited){throw 'isolated host exited'};if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Label"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer activity-followup-synthetic'};TimeoutSec=15};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 80 -Compress))};Invoke-RestMethod @p}
function Capture{Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 90}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$script:service=$desktop.Child;Await {if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} backend;$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'host stops normally';$desktop.Dispose();$script:desktop=$null;$script:service=$null}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 70|ConvertFrom-Json -AsHashtable}
function Begin($Name,[int]$Seconds=1,[int]$Timeout=30){
 $script:case=$Name;$script:beforeCalls=Requests;$script:beforeUsers=@((Api '/api/chat').messages|Where-Object role -eq user).Count
 $responses=@(@{version=1;kind='call';text=$null;calls=@(@{id=('enqueue-'+$Name);tool='activity.enqueue';args=@{commands=@(@{kind='wait';seconds=$Seconds});follow_up=@{topic=('合成后续 '+$Name);timeout_seconds=$Timeout}}})},@{version=1;kind='reply';text=('FIRST_'+$Name);calls=@()},@{version=1;kind='reply';text=('RESULT_'+$Name);calls=@()})
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@($responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding UTF8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=('合成测试：等这个动作结束再告诉我结果 '+$Name)}
 Await {@((Api '/api/chat').messages|Where-Object text -eq ('FIRST_'+$Name)).Count-eq1} 'initial dialogue'
 $script:followId=(Api '/api/followups').items[0].id
 Check ([bool]$script:followId) 'a real follow-up is persisted by the initial reply'
}
function Result($Name){
 Await {@((Api '/api/chat').messages|Where-Object text -eq ('RESULT_'+$Name)).Count-eq1} ('automatic result '+$Name) 35
 $event=Model-CurrentEvent (Capture)
 Check ([bool]$event.scheduled_intent.follow_up) 'the automatic call receives the actual bound follow-up context'
 Check (@((Api '/api/chat').messages|Where-Object role -eq user).Count-eq($script:beforeUsers+1)) 'no second user message is needed'
 Check ((Requests)-eq($script:beforeCalls+3)) 'one enqueue call, one initial reply and one follow-up actor call'
 Await {@((Api '/api/followups').items|Where-Object {$_.id-eq$script:followId-and$_.status-eq'completed'-and$_.spoke}).Count-eq1} 'durable follow-up outcome'
 $script:resultEvent=$event
}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $root provider/port)} provider;$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 @{nonce='approval';responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root provider/review-plan.json) -Encoding UTF8
 $character=Join-Path $root character;New-Item -ItemType Directory -Path $character|Out-Null
 @{schemaVersion=1;version=1;id='followup-synthetic';displayName='Followup Fixture';notice='Synthetic test';identity='Synthetic role';attention=@('actual outcomes');boundaries=@('Only use supplied receipts');character=@{identity='Synthetic role';attention=@('actual outcomes');voice=@('Plain short responses.')}}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $character persona.json) -Encoding UTF8
 @{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='Initial';description='Synthetic';next='';minimumEvidence=0})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $character stages.json) -Encoding UTF8
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.cognition=@{enabled=$false};$cfg.reflectOnActivities=$false;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.plugins=@{widget=@{enabled=$true;roam=$false;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;showBubble=$false;reactionCacheEnabled=$false}}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Synthetic Actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='reviewer';label='Synthetic Reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true});$cfg.review=@{enabled=$true;profileId='reviewer';maxRewrites=0};$cfg.backgroundBudget=@{enabled=$true;maxCallsPerDay=120;maxInputTokensPerDay=500000}
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 70|Set-Content -LiteralPath $config -Encoding UTF8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 Begin complete;Result complete
 Check ($resultEvent.scheduled_intent.trigger.all_terminal-and$resultEvent.scheduled_intent.trigger.outcomes[0].status-eq'completed') 'completion is based on the actual completed activity'
 $count=Requests;Start-Sleep -Seconds 2;Check ((Requests)-eq$count-and@((Api '/api/chat').messages|Where-Object text -eq RESULT_complete).Count-eq1) 'completed callbacks do not repeatedly call or repeat the result'
 Begin pause 20 30;$null=Api '/api/control' @{paused=$true};$count=Requests;Start-Sleep -Seconds 2;Check ((Requests)-eq$count) 'manual pause prevents a callback provider request'
 $null=Api '/api/control' @{paused=$false};Result pause
 Check ($resultEvent.scheduled_intent.trigger.outcomes[0].status-ne'completed') 'interruption is reported as an actual interruption instead of completion'
 Begin timeout 20 5;Result timeout
 Check (!$resultEvent.scheduled_intent.trigger.all_terminal-and$resultEvent.scheduled_intent.trigger.outcomes[0].status-eq'not_confirmed') 'timeout does not invent a completed action'
 Begin restart 30 60;Stop-Backend;Start-Backend;Result restart
 Check ($resultEvent.scheduled_intent.trigger.outcomes[0].status-match'application_restarted') 'a real restart preserves the promise and exposes the interruption'
 $settings=Settings;$settings.backgroundBudget.maxCallsPerDay=1;$null=Api '/api/settings' @{settings=$settings}
 Begin budget 1 30
 Await {@((Api '/api/followups').items|Where-Object deferred_budget -eq $true).Count-eq1} 'budget deferral' 15
 Check ((Requests)-eq($beforeCalls+2)) 'exhausted background budget does not make a provider request or erase the follow-up'
 $settings=Settings;$settings.backgroundBudget.maxCallsPerDay=120;$null=Api '/api/settings' @{settings=$settings};Result budget
 Check (@((Api '/api/followups').items|Where-Object status -eq active).Count-eq0) 'budget recovery automatically settles the pending result without another user turn'
 Check ((Api '/api/followups').private_content_included-eq$false) 'management diagnostics contain no dialogue or memory body'
 $slots=@((Api '/api/state').jobs|Where-Object id -like 'routine.followup-slot-*')
 Check ($slots.Count-eq1-and$slots[0].run_token-ge5) 'successive promises reuse a durable slot with monotonic execution tokens instead of exhausting the queue'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;paid_model_calls=0;production_data_accessed=$false}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding UTF8
 Write-Host "Activity follow-up passed: $($checks.Count) checks. Evidence: $root"
}catch{
 @{completed=$false;checks=$checks;failure=$_.Exception.Message;root=$root;paid_model_calls=0}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root failure.json) -Encoding UTF8;throw
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
