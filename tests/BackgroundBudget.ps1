param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('background-budget-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='background-budget-session-fixture';$env:HIBERNATION_BUDGET_KEY='background-provider-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(15);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer background-budget-session-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))};Invoke-RestMethod @p}
function Start-Backend{$script:service=Start-Process $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log);Wait-For {if($script:service.HasExited){throw 'backend exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$script:service.Id}} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};$service.WaitForExit(10000)|Out-Null;Check $service.HasExited 'graceful shutdown'}
function Turn([bool]$Background=$false){$body=@{request_id=[Guid]::NewGuid().ToString('N');text='本地预算验证'};if($Background){$body.kind='outer_event';$null=Api '/api/event' $body}else{$null=Api '/api/message' $body};Wait-For {!(Api '/api/chat').busy} 'turn complete'}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 40|ConvertFrom-Json -AsHashtable}
function Save($Value){$null=Api '/api/settings' @{settings=$Value}}
function Calls{(Invoke-RestMethod ($fixture+'/fixture/health')).calls}
try{
 $provider=Start-Process $ProviderExecutable -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Wait-For {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.profiles=@($cfg.profiles[0]);$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles+=@{id='actor';label='actor';enabled=$true;protocol='chat-completions';origin=$fixture;basePath='/v1';model='fixture-model';credentialEnvironment='HIBERNATION_BUDGET_KEY';supportsJsonObject=$true;supportsJsonSchema=$false;timeoutMs=10000;maxOutputTokens=2048}
 $cfg.profiles+=@{id='reviewer';label='reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;basePath='/v1';model='fixture-reviewer';credentialEnvironment='HIBERNATION_BUDGET_KEY';supportsJsonObject=$true;supportsJsonSchema=$false;timeoutMs=10000;maxOutputTokens=2048}
 $cfg.review=@{enabled=$false;profileId='reviewer';maxRewrites=0};$cfg.backgroundBudget=@{enabled=$true;maxCallsPerDay=1;maxInputTokensPerDay=1000};$cfg|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root config.json) -Encoding utf8
 @{version=1;kind='reply';text='验证完成。';calls=@()}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $root provider/response.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 Turn $true;$b=Api '/api/background-budget';Check ($b.used_calls-eq1-and$b.blocked_reason-eq'daily_calls'-and$b.unknown_usage_calls-eq1-and$b.reported_calls-eq0) 'daily cap counts unknown-usage background request without inventing input tokens'
 $before=Calls;Turn $true;Check ((Calls)-eq$before) 'exhausted background cap makes no provider request'
 Turn;Check ((Calls)-eq($before+1)-and(Api '/api/background-budget').used_calls-eq1) 'foreground conversation remains available and does not consume daily background budget'
 Stop-Backend;Start-Backend;Check ((Api '/api/background-budget').used_calls-eq1) 'restart preserves daily usage'
 $settings=Settings;$settings.backgroundBudget.maxCallsPerDay=10;Save $settings
 @{prompt_tokens=1200;completion_tokens=10}|ConvertTo-Json|Set-Content (Join-Path $root provider/usage.json) -Encoding utf8
 Turn $true;$b=Api '/api/background-budget';Check ($b.used_calls-eq2-and$b.input_tokens-eq1200-and$b.reported_calls-eq1-and$b.blocked_reason-eq'daily_input_tokens') 'reported tokens gate the next background request'
 $before=Calls;Turn $true;Check ((Calls)-eq$before) 'observed token cap prevents next background provider request'
 $view=(Api '/api/dashboard').runtime_status;Check ($view.code-eq'background_budget'-and!$view.background_allowed-and$null-ne$view.speech_state) 'dashboard distinguishes background budget wait from silence or failure'
 $settings=Settings;$settings.backgroundBudget.enabled=$false;Save $settings;Turn $true;Check ((Api '/api/background-budget').used_calls-eq3-and!(Api '/api/background-budget').blocked) 'disabling separate cap preserves usage while allowing background work'
 $settings=Settings;$settings.backgroundBudget.enabled=$true;Save $settings;Check ((Api '/api/background-budget').blocked) 'reenabling does not erase used allowance'
 $settings=Settings;$settings.backgroundBudget.maxCallsPerDay=5;$settings.backgroundBudget.maxInputTokensPerDay=100000;$settings.review.enabled=$true;Save $settings
 @{nonce='repair';responses=@(@{response='<｜｜DSML｜｜ calls>untrusted tool-like text</｜｜DSML｜｜ calls>'},@{response=@{version=1;kind='reply';text='验证完成。';calls=@()}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 @{nonce='approve';responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/review-plan.json) -Encoding utf8
 $before=Calls;Turn $true;$event=(Api '/api/state').jobs.Where({$_.id-eq'event'})[0];$trace=Api '/api/trace' @{id=('trace-event-'+$event.run_token)}
 Check ((Calls)-eq($before+2)-and(Api '/api/background-budget').used_calls-eq5-and$trace.call_error.code-eq'background_daily_call_budget') 'background repair consumes daily calls and independent reviewer is gated before provider request'
 $null=Api '/api/control' @{paused=$true};Check ((Api '/api/dashboard').runtime_status.code-eq'manual_pause') 'manual pause has its own diagnostic state'
 Stop-Backend;Start-Backend;Check ((Api '/api/dashboard').runtime_status.code-eq'manual_pause'-and(Api '/api/background-budget').used_calls-eq5) 'restart preserves manual pause and daily allowance'
 $settings=Settings;$settings.backgroundBudget.maxCallsPerDay=10;$settings.backgroundBudget.maxInputTokensPerDay=100000;$settings.review.enabled=$false;Save $settings;$null=Api '/api/control' @{paused=$false}
 @{prompt_tokens=12000;completion_tokens=10;prompt_cache_hit_tokens=11900;prompt_cache_miss_tokens=100}|ConvertTo-Json|Set-Content (Join-Path $root provider/usage.json) -Encoding utf8
 $prior=Api '/api/background-budget';Turn $true;$now=Api '/api/background-budget'
 Check ($now.input_tokens-$prior.input_tokens-eq100-and$now.total_input_tokens-$prior.total_input_tokens-eq12000-and$now.cached_input_tokens-$prior.cached_input_tokens-eq11900) 'actual provider cache hits are excluded while total input stays traceable'
 $settings=Settings;$settings.backgroundBudget.autoEconomy=$true;Save $settings
 Wait-For {$null-ne(Api '/api/dashboard').idle_economy.input_idle_seconds} 'native machine idle sample'
 $economy=(Api '/api/dashboard').idle_economy
 Check ($economy.enabled-and!$economy.reads_input_content-and$economy.mode-in@('active','idle','deep_idle')) 'operator setting enables native input-idle observation without input content'
 Check ((Settings).backgroundBudget.autoEconomy-eq$true-and(Api '/api/background-budget').used_calls-eq$now.used_calls) 'economy configuration preserves observed budget counters'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Background budget integration passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
