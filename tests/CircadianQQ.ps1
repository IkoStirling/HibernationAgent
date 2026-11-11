param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$Node)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot);Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/circadian-qq-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='circadian-qq-fixture';$env:HA_CIRCADIAN_QQ_API='synthetic-qq-api';$env:HA_CIRCADIAN_QQ_EVENT='synthetic-qq-events';$provider=$null;$desktop=$null;$service=$null;$checks=0;$start=[DateTime]::UtcNow
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=30){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if($service-and$service.HasExited){throw 'QQ fixture host exited'};if(&$Probe){return};Start-Sleep -Milliseconds 80};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer circadian-qq-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))};Invoke-RestMethod @p}
function Transport{((Api '/api/capabilities').plugins|Where-Object id -eq 'napcat')[0].current}
function Fixture{(Invoke-RestMethod -Uri ($script:fixture+'/fixture/status') -TimeoutSec 5).data}
function Plugin($Name,$Arguments){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Await {$script:receipt=Api '/api/plugin/result';$script:receipt.state-in@('succeeded','failed')} $Name;if(!$script:receipt.result.ok){throw 'sleep fixture configuration rejected'};return $script:receipt.result.result}
function QQ($Id,$Text){
 $message=@{self_id=10001;user_id='20001';message_id=$Id;time=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds();post_type='message';message_type='private';sender=@{user_id='20001';nickname='Synthetic owner'};message=@(@{type='text';data=@{text=$Text}})}
 $body=[Text.Encoding]::UTF8.GetBytes(($message|ConvertTo-Json -Depth 10 -Compress));$hmac=[Security.Cryptography.HMACSHA1]::new([Text.Encoding]::UTF8.GetBytes('synthetic-qq-events'));try{$signature='sha1='+[Convert]::ToHexString($hmac.ComputeHash($body)).ToLowerInvariant()}finally{$hmac.Dispose()}
 $null=Invoke-RestMethod -Method Post -Uri ('http://127.0.0.1:'+$script:callback+'/onebot/events') -Headers @{'X-Signature'=$signature;'X-Self-ID'='10001'} -ContentType application/json -Body $body -TimeoutSec 5
}
try{
 $provider=Start-Process -FilePath $Node -ArgumentList @(('"'+(Join-Path $PSScriptRoot CircadianQQFixture.mjs)+'"'),('"'+(Join-Path $root provider)+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log);Await {Test-Path -LiteralPath (Join-Path $root provider/port)} fixture;$script:fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$script:callback=$listener.LocalEndpoint.Port;$listener.Stop()
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.cognition=@{enabled=$true};$cfg.solitudeIntervalMs=43200000;$cfg.idleBeforeSolitudeMs=43200000;$cfg.computer=@{enabled=$false;allowScreenCapture=$false;readRoots=@()}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Local synthetic QQ actor';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';autonomousMode=$false;roam=$false;reactionCacheEnabled=$false;showBubble=$false};rhythm=@{enabled=$true;automaticRest=$false;circadianEnabled=$true;idleBeforeRestSeconds=5};napcat=@{enabled=$true;selfUserId='10001';ownerUserId='20001';apiOrigin=$fixture;apiTokenEnvironment='HA_CIRCADIAN_QQ_API';eventSecretEnvironment='HA_CIRCADIAN_QQ_EVENT';callbackPort=$callback;cooldownSeconds=1;mergeWindowMs=0;reviewReplies=$false}}
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 40|Set-Content -LiteralPath $config -Encoding utf8NoBOM
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child;Await {Test-Path -LiteralPath $ready} startup;$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'};Await {(Transport).connection.account_online} connection
 $minute=(Get-Date).Hour*60+(Get-Date).Minute;$null=Plugin 'rhythm.configure' @{expected_revision=0;preferences=@{enabled=$true;start_minute=($minute+1439)%1440;end_minute=($minute+120)%1440;drag_wake_probability=0;message_wake_probability=0}}
 Await {(Api '/api/rhythm/status').phase-eq'sleeping'} sleep;QQ '91001' 'SYNTHETIC_SLEEP_QQ_INPUT';Await {@((Api '/api/mind/trace').events|Where-Object {$_.kind-eq'cognition.sleep.input'-and$_.status-eq'held_without_model'}).Count-ge1-and(Transport).transport.queued-eq0-and!(Transport).busy} held
 Check ((Fixture).calls-eq0-and(Fixture).sends-eq0) 'real callback bridge retains sleeping QQ input without invoking model or sending a notice'
 Check ((Api '/api/rhythm/status').phase-eq'sleeping') 'shared desktop body stays asleep after QQ input'
 $null=Api '/api/rhythm/wake' @{};Start-Sleep -Seconds 2
 Check ((Fixture).calls-eq0-and(Fixture).sends-eq0) 'waking does not replay an old QQ input'
 QQ '91002' 'SYNTHETIC_FRESH_AWAKE_QQ_INPUT';Await {(Fixture).sends-eq1-and!(Transport).busy} fresh_reply
 Check ((Fixture).calls-eq1-and(Transport).callbacks-eq2) 'a fresh awake QQ message resumes through exactly one local model call'
 $capture=Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl) -Raw|ConvertFrom-Json;$contents=($capture.messages|ForEach-Object content)-join"`n"
 Check ($contents-match'sleep_inertia'-and$contents-match'SYNTHETIC_FRESH_AWAKE_QQ_INPUT') 'QQ model sees current continuous body state and fresh input'
 $sent=Get-Content -LiteralPath (Join-Path $root provider/sent.jsonl) -Raw|ConvertFrom-Json
 Check ($sent.user_id-eq'20001'-and($sent.message|ConvertTo-Json -Depth 10)-match'SYNTHETIC_AWAKE_QQ_REPLY') 'only the synthetic owner receives the fresh reply on the local fixture'
 Check ((Api '/api/dashboard').health-eq'ok') 'shared host remains healthy after held and active QQ sessions'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'native QQ fixture checkpoints and stops'
 @{completed=$true;checks=$checks;paid_model_calls=0;local_requests=(Fixture).calls;local_qq_sends=(Fixture).sends;real_elapsed_seconds=([DateTime]::UtcNow-$start).TotalSeconds;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8NoBOM
 Write-Output "Circadian QQ checks passed: $checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
