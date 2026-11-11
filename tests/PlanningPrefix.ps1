param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$base=Join-Path $BuildRoot ('integration/planning-prefix-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $base|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs);$env:HIBERNATION_SESSION_KEY='planning-prefix-fixture';$checks=0;$captures=@();$desktop=$null;$service=$null;$provider=$null
function Check($v,$name){if(!$v){throw "FAILED: $name"};$script:checks++;Write-Output "PASS $name"}
function Await([scriptblock]$probe,$name,[int]$Seconds=45){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $name"}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer planning-prefix-fixture'};TimeoutSec=12};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
try{
 foreach($mode in @('empty','occupied')){
  $root=Join-Path $base $mode;New-Item -ItemType Directory -Force $root|Out-Null
  $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
  $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.backgroundBudget=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.contextControl=@{enabled=$false}
  $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Stable planning fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
  $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;reactionCacheEnabled=$false;showBubble=$false}}
  $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8
  $responses=@(@{response=@{version=1;commands=@()}})
  if($mode-eq'occupied'){$responses=@(@{response=@{version=1;kind='call';text=$null;calls=@(@{id='seed';tool='activity.enqueue';args=@{commands=@(@{kind='wait';seconds=120},@{kind='wait';seconds=120})}})}},@{response=@{version=1;kind='reply';text='先静静待一会儿。';calls=@();presentation=@{speech=$false;bubble=$false}}},@{response=@{version=1;commands=@(1..5|ForEach-Object {@{kind='wait';seconds=1}})}})}
  @{nonce=$mode;responses=$responses}|ConvertTo-Json -Depth 45|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
  $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$service=$desktop.Child
  Await {Test-Path (Join-Path $root ready.json)} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
  if($mode-eq'occupied'){$null=Api '/api/message' @{request_id='occupied';text='安排两个有限等待。'};Await {!(Api '/api/chat').busy} 'seed queued';Check (!(Api '/api/chat').call_failed) 'occupied queue seeded'}
  Await {$status=Api '/api/activities/status';if($mode-eq'empty'){$status.planning_cadence.quiet-eq1}else{$status.last_error-eq'activity_unavailable_kind'}} 'actual background plan'
  $wire=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 80;$scene=$wire.messages[1].content|ConvertFrom-Json -Depth 70;$captures+=@{mode=$mode;system=$wire.messages[0].content;slots=$scene.queue_slots}
  $status=Api '/api/activities/status';if($mode-eq'occupied'){Check ($scene.queue_slots-eq4-and$status.queued-eq1-and$status.active.kind-eq'wait') 'actual four-slot allowance rejects five commands despite stable six-slot declaration'}
  $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown';$desktop.Dispose();$desktop=$null;Stop-Process -Id $provider.Id;$provider=$null
 }
 Check ($captures[0].slots-eq6-and$captures[1].slots-eq4) 'different execution capacities observed'
 Check ($captures[0].system-ceq$captures[1].system) 'stable system prefix does not change with queue occupancy'
 @{completed=$true;checks=$checks;root=$base;system_bytes=[Text.Encoding]::UTF8.GetByteCount($captures[0].system);slots=@($captures|ForEach-Object slots)}|ConvertTo-Json|Set-Content (Join-Path $base acceptance.json) -Encoding utf8
 Write-Output "Planning prefix passed: $checks checks. $base"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
