param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('call-budget-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='call-budget-session-fixture-20261027';$env:HIBERNATION_BUDGET_KEY='budget-provider-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(15);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer call-budget-session-fixture-20261027'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))};Invoke-RestMethod @p}
function Start-Backend{$script:service=Start-Process $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log);Wait-For {if($script:service.HasExited){throw 'backend exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$script:service.Id}} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};$service.WaitForExit(10000)|Out-Null;Check $service.HasExited 'graceful shutdown'}
function Idle{Wait-For {!(Api '/api/chat').busy} 'turn complete'}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Idle}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@()}}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 40|ConvertFrom-Json -AsHashtable}
function Save($Value){$null=Api '/api/settings' @{settings=$Value}}
try{
 $provider=Start-Process $ProviderExecutable -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Wait-For {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.profiles=@($cfg.profiles[0]);$cfg.maxToolRounds=2;$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000
 foreach($id in @('actor','reviewer')){$cfg.profiles+=@{id=$id;label=$id;enabled=$true;protocol='chat-completions';origin=$fixture;basePath='/v1';model=$(if($id-eq'reviewer'){'fixture-reviewer'}else{'fixture-model'});credentialEnvironment='HIBERNATION_BUDGET_KEY';supportsJsonObject=$true;supportsJsonSchema=$false;timeoutMs=10000;maxOutputTokens=2048}}
 $cfg.review=@{enabled=$true;profileId='reviewer';maxRewrites=2};$cfg|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root config.json) -Encoding utf8
 @{nonce='approve';responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/review-plan.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 $tool=@{version=1;kind='call';text=$null;calls=@(@{id='write';tool='workspace.write';args=@{path='once.md';content='one'}})}
 Plan @('<｜｜DSML｜｜ calls>untrusted tool-like text</｜｜DSML｜｜ calls>',$tool,(Reply '写好了。'));Message '格式修复不能吃掉工具轮数'
 $chat=Api '/api/chat';Check ($chat.messages[-1].text-eq'写好了。'-and!$chat.call_failed) 'a format repair does not consume a tool round or prevent final reply'
 Check ((Get-Content (Join-Path $cfg.dataRoot drafts/once.md) -Raw)-eq'one') 'only validated and reviewed JSON commands execute'
 $request=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 50;$review=Get-Content (Join-Path $root provider/review-last-request.json) -Raw|ConvertFrom-Json -Depth 50
 Check ($request.response_format.type-eq'json_object'-and$review.response_format.type-eq'json_object') 'actor and independent reviewer request explicit JSON object mode'
 $b=(Api '/api/dashboard').model_budget;Check ($b.used-eq5-and$b.limit-eq600-and$b.remaining-eq595) 'budget accounts for actor repairs plus both independent approvals'
 $settings=Settings;$settings.review.enabled=$false;$settings.maxToolRounds=6;$settings.maxStartsPerHour=$b.used+1;Save $settings
 Plan @((Reply '继续。'));$null=Api '/api/event' @{request_id=[Guid]::NewGuid().ToString('N');kind='outer_event';text='后台预算预留验证'};Idle
 $event=(Api '/api/state').jobs.Where({$_.id-eq'event'})[0];$eventTrace=Api '/api/trace' @{id=('trace-event-'+$event.run_token)}
 Check ($eventTrace.call_error.code-eq'background_budget_reserved'-and(Api '/api/dashboard').model_budget.used-eq$b.used) 'background processing yields before spending the foreground reserve'
 Message '保留额度用于用户交谈';Check ((Api '/api/chat').messages[-1].text-eq'继续。') 'foreground conversation can use reserved calls'
 $callsBefore=(Invoke-RestMethod ($fixture+'/fixture/health')).calls;Message '本地小时额度耗尽验证';$callIssue=(Api '/api/chat').call_error
 Check ($callIssue.code-eq'local_hourly_budget'-and$callIssue.http_status-eq0-and$callIssue.retry_after_seconds-gt0-and$callIssue.message.Contains('应用每小时')) 'hourly denial identifies application source and reset estimate'
 Check ((Invoke-RestMethod ($fixture+'/fixture/health')).calls-eq$callsBefore) 'local hourly denial makes no provider request'
 $used=(Api '/api/dashboard').model_budget.used;$settings=Settings;$settings.maxStartsPerHour=600;Save $settings
 Message '提高本地额度后立即恢复';Check (!(Api '/api/chat').call_failed-and(Api '/api/dashboard').model_budget.used-eq($used+1)) 'raising budget restores calls live without clearing prior usage'
 $settings=Settings;$settings.maxToolRounds=1;$settings.profiles.Where({$_.id-eq'actor'})[0].supportsJsonObject=$false;Save $settings
 Plan @($tool);Message '独立工具轮数限制';Check ((Api '/api/chat').call_error.code-eq'tool_round_limit') 'tool round exhaustion is distinct from hourly and provider limits'
 $used=(Api '/api/dashboard').model_budget.used;Stop-Backend;Start-Backend
 Check ((Api '/api/dashboard').model_budget.used-eq$used) 'restarting backend preserves hourly usage'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Call budget integration passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
