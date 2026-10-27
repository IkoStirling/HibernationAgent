param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/reaction-cadence-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='reaction-cadence-fixture';$service=$null;$provider=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Now{[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};try{Status|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root timeout.json) -Encoding utf8}catch{};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer reaction-cadence-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Response){@{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$Response})}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
function Status{Api '/api/reactions/status'}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'reaction cadence host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'reaction cadence host stops cleanly';$desktop.Dispose()}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Reaction cadence fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$false;reactionCacheEnabled=$true;showBubble=$false}}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8;Plan @{version=1;reactions=@()};Start-Backend;$null=Api '/api/model' @{id='actor'};$null=Api '/api/reactions/refill' @{}
 Await {(Status).preparation_cadence.quiet-eq1} 'empty reaction choice';$status=Status;$left=$status.next_preparation_ms-(Now)
 Check ($status.last_error-eq''-and$status.available-eq0-and$status.preparation_cadence.failures-eq0-and$left-gt100000-and$left-le125000) 'no suitable reaction is a successful quiet choice with delayed preparation'
 $before=Requests;Check ($before-eq1) 'quiet choice uses one actual local request'
 Stop-Backend;Start-Backend;Await {(Status).next_preparation_ms-(Now)-gt50000} 'restored quiet clock';Check ((Status).preparation_cadence.quiet-eq1-and(Requests)-eq$before) 'normal restart retains quiet backoff without a new model request'
 Plan 'not valid reaction JSON';$null=Api '/api/reactions/refill' @{};Await {(Status).last_error-eq'reaction_format'} 'invalid batch';$status=Status
 Check ($status.preparation_cadence.failures-eq1-and$status.preparation_cadence.quiet-eq0-and$status.available-eq0) 'invalid output has a separate failure streak and creates no reaction'
 Plan @{version=1;reactions=@()};$null=Api '/api/reactions/refill' @{};Await {(Status).preparation_cadence.quiet-eq1} 'quiet choice after failure'
 $null=Api '/api/reactions/refill' @{};Await {(Status).preparation_cadence.quiet-eq2} 'second quiet choice';Check ((Status).next_preparation_ms-(Now)-gt220000) 'repeated quiet choices gradually extend preparation rather than polling every minute'
 Plan @{version=1;reactions=@(@{text='这里先留给我站一会儿。';animation='idle'})};$settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.profiles[1].model='fixture-model-next';$before=Requests;$null=Api '/api/settings' @{settings=$settings}
 Await {(Status).available-eq1} 'new context candidate';$status=Status
 Check ($status.preparation_cadence.quiet-eq0-and$status.preparation_cadence.failures-eq0-and(Requests)-eq$before+1) 'changed model context restores a single future preparation and clears obsolete cadence'
 Check ((Api '/api/chat').messages.Count-eq0-and$status.intent_mode-eq'cache_only') 'background preparation remains outside dialogue and never waits on a gesture'
 Stop-Backend;@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Reaction cadence passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
