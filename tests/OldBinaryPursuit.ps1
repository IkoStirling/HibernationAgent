param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$OlderBuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/old-pursuit-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
'hibernation.activity-recovery.v1'|Set-Content (Join-Path $root .activity-recovery-fixture) -Encoding utf8NoBOM
$env:HIBERNATION_SESSION_KEY='synthetic-old-pursuit';$service=$null;$provider=$null;$checks=0
function Check($condition,$name){if(!$condition){throw "FAILED: $name"};$script:checks++;Write-Output "PASS $name"}
function Await([scriptblock]$probe,$name){$until=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$until){if($service-and$service.HasExited){throw 'synthetic backend exited'};if(&$probe){return};Start-Sleep -Milliseconds 70};throw "TIMEOUT $name"}
function Api($path,$body=$null){$a=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer synthetic-old-pursuit'};TimeoutSec=10};if($null-ne$body){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Compress -Depth 40))};Invoke-RestMethod @a}
function Requests{if(Test-Path (Join-Path $root provider/requests.jsonl)){return @(Get-Content (Join-Path $root provider/requests.jsonl)).Count};0}
function Start-Backend($runtime){
 $ready=Join-Path $root ready.json;$previous=if(Test-Path $ready){(Get-Item $ready).LastWriteTimeUtc}else{[DateTime]::MinValue}
 $script:service=Start-Process -FilePath (Join-Path $runtime hibernation-agent.exe) -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$ready+'"') -WorkingDirectory $ProjectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log)
 Await {if(Test-Path $ready){$r=Get-Content $ready -Raw|ConvertFrom-Json;if($r.pid-eq$service.Id-and(Get-Item $ready).LastWriteTimeUtc-gt$previous){$script:origin='http://127.0.0.1:'+$r.port;return $true}};$false} 'synthetic backend ready'
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'synthetic backend saves cleanly';$script:service=$null}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Await {Test-Path (Join-Path $root provider/port)} 'synthetic provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $root no-plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.frontend.startupAnimation=$false;$cfg.cognition=@{enabled=$false};$cfg.review=@{enabled=$false};$cfg.memoryWorkshop=@{enabled=$false};$cfg.skills=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.backgroundBudget=@{enabled=$false}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Local rollback fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;supportsStreaming=$false})
 $cfg|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root config.json) -Encoding utf8NoBOM
 & (Join-Path $BuildRoot Hibernation_ActivityRecoveryFixture.exe) $root pursuit-kind
 if($LASTEXITCODE-ne0){throw 'synthetic seed failed'}
 $key=Join-Path $root data/runtime/memory-key.dpapi;$keyHash=(Get-FileHash $key).Hash
 Start-Backend $OlderBuildRoot
 Check ((Api '/api/state').control.paused) 'older binary starts with the durable pause still set'
 Check ((Requests)-eq0) 'paused older binary invokes no actor'
 $null=Api '/api/control' @{paused=$false}
 Await {$job=@((Api '/api/state').jobs|Where-Object id -eq 'autonomy.personal-pursuit')[0];$job.state-eq'failed'} 'unknown dedicated job fails closed'
 Check ((Requests)-eq0) 'older binary never treats the new job as generic solitude'
 $null=Api '/api/control' @{paused=$true};Stop-Backend
 & (Join-Path $BuildRoot Hibernation_ActivityRecoveryFixture.exe) $root inspect-pursuit-kind
 Check ($LASTEXITCODE-eq0-and(Get-FileHash $key).Hash-eq$keyHash) 'the original encrypted record and key survive older startup'
 Start-Backend $BuildRoot
 Check ((Api '/api/state').control.paused) 'current binary keeps the recovery pause'
 $null=Api '/api/control' @{paused=$false}
 Await {(Api '/api/chat').can_send} 'current foreground becomes available after startup reconciliation'
 $beforeForeground=Requests
 @{nonce='current';responses=@(@{response=@{version=1;kind='reply';text='回到当前版本了。';calls=@();presentation=@{speech=$false;bubble=$false}}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8NoBOM
 $request='roundtrip-'+[guid]::NewGuid().ToString('N');$null=Api '/api/message' @{request_id=$request;text='合成存档版本往返检查。'}
 Await {$state=Api '/api/state';$state.messages|Where-Object {$_.request_id-eq$request-and$_.kind-eq'assistant_message'}} 'current foreground reply'
 Check (!(Api '/api/chat').call_failed-and(Requests)-eq($beforeForeground+1)) 'current binary can converse after the old kind was retired, using only the local fixture'
 $null=Api '/api/control' @{paused=$true};Stop-Backend
 & (Join-Path $BuildRoot Hibernation_ActivityRecoveryFixture.exe) $root inspect-pursuit-kind
 Check ($LASTEXITCODE-eq0-and(Get-FileHash $key).Hash-eq$keyHash) 'version roundtrip preserves the original encrypted record and key'
 Check (!([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $root data/archive/records.sqlite)))).Contains('SYNTHETIC_NOTE_KEEP')) 'test record is not plaintext in the database'
 @{completed=$true;checks=$checks;root=$root;older_host_sha256=(Get-FileHash (Join-Path $OlderBuildRoot hibernation-agent.exe)).Hash;current_host_sha256=(Get-FileHash (Join-Path $BuildRoot hibernation-agent.exe)).Hash;paid_calls=0;formal_data_used=$false;general_downgrade_support_claimed=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8NoBOM
 Write-Output "Old dedicated job test passed: $checks checks. $root"
}catch{
 try{$state=Api '/api/state';$chat=Api '/api/chat';$dash=Api '/api/dashboard';@{error=$_.Exception.Message;control=$state.control;health=$state.health;jobs=$state.jobs;can_send=$chat.can_send;busy=$chat.busy;runtime=$dash.runtime_status;cognitive_trace=$dash.cognitive_trace;requests=(Requests)}|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root failure.json) -Encoding utf8NoBOM}catch{}
 throw
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}}}
