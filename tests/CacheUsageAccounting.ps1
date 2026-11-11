param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('cache-accounting-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='cache-accounting-fixture-only'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$until=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$until){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer cache-accounting-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Start-Backend{$script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log);Await {if($service.HasExited){throw 'cache accounting fixture exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'fixture saves and stops cleanly'}
function Message{$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='核对本地用量回执。'};Await {!(Api '/api/chat').busy} 'turn';Check (!(Api '/api/chat').call_failed) 'local fixture reply succeeds'}
try{
 $providerRoot=Join-Path $root provider;$provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 @{version=1;kind='reply';text='这次回执记下了。';calls=@()}|ConvertTo-Json -Depth 15|Set-Content (Join-Path $providerRoot response.json) -Encoding utf8
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000;$cfg.review=@{enabled=$false}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Accounting fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;supportsStreaming=$true});$cfg|ConvertTo-Json -Depth 60|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'}
 @{completion_tokens=10}|ConvertTo-Json|Set-Content (Join-Path $providerRoot usage.json) -Encoding utf8;Message;$usage=(Api '/api/model-cache').connections.actor.total
 Check ($usage.calls-eq1-and$usage.output_tokens-eq10-and$usage.input_tokens_known-eq$false-and$usage.output_tokens_known-eq$true) 'output-only report leaves input unknown instead of inventing zero'
 Check ($usage.input_reported_calls-eq0-and$usage.output_reported_calls-eq1-and$usage.complete_usage_reported_calls-eq0) 'partial reporting is counted per actual field'
 @{prompt_tokens=0;completion_tokens=0;prompt_cache_hit_tokens=0;prompt_cache_miss_tokens=0}|ConvertTo-Json|Set-Content (Join-Path $providerRoot usage.json) -Encoding utf8;Message;$usage=(Api '/api/model-cache').connections.actor.total
 Check ($usage.input_tokens_known-eq$true-and$usage.input_tokens-eq0-and$usage.input_reported_calls-eq1-and$usage.output_reported_calls-eq2-and$usage.complete_usage_reported_calls-eq1) 'explicit provider zero remains a real zero report'
 Check ($usage.cache_reported_calls-eq1-and$null-eq$usage.cache_hit_ratio) 'zero-sized cache report does not fabricate a hit ratio'
 Remove-Item -LiteralPath (Join-Path $providerRoot usage.json);Message;$saved=(Api '/api/model-cache').connections.actor
 Check ($saved.total.calls-eq3-and$saved.total.usage_reported_calls-eq2-and$saved.total.input_reported_calls-eq1-and$saved.total.output_tokens-eq10) 'missing usage still records the request without fabricated tokens'
 Check ((Invoke-RestMethod ($fixture+'/fixture/health')).calls-eq3) 'accounting adds no provider calls or retries'
 Stop-Backend;Start-Backend;$restored=(Api '/api/model-cache').connections.actor
 Check (($restored|ConvertTo-Json -Depth 30 -Compress)-ceq($saved|ConvertTo-Json -Depth 30 -Compress)) 'field knowledge and real counters persist across restart'
 Stop-Backend;@{completed=$true;checks=$checks;root=$root;external_api_calls=0;provider_fixture_calls=3}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Cache usage accounting passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
