param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$base=Join-Path $BuildRoot ('integration/planning-repair-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $base|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs);$env:HIBERNATION_SESSION_KEY='planning-repair-fixture';$checks=0;$captures=@();$desktop=$null;$service=$null;$provider=$null
function Check($v,$name){if(!$v){throw "FAILED: $name"};$script:checks++;Write-Output "PASS $name"}
function Await([scriptblock]$probe,$name,[int]$Seconds=45){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $name"}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer planning-repair-fixture'};TimeoutSec=12};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
try{
 foreach($mode in @('repaired','budget','invalid')){
  $root=Join-Path $base $mode;New-Item -ItemType Directory -Force $root|Out-Null
  $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
  $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.backgroundBudget=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.contextControl=@{enabled=$false}
  $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Stable planning fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
  $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;reactionCacheEnabled=$false;showBubble=$false}}
  $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8
  $initial=@{version=1;commands=@(@{kind='wait';seconds=1},@{kind='move_to';x=.2;y=.2})};$fixed=@{version=1;commands=@(@{kind='wait';seconds=1})}
  if($mode-eq'budget'){$cfg.backgroundBudget=@{enabled=$true;maxCallsPerDay=1;maxInputTokensPerDay=500000;autoEconomy=$false}}
  $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8
  $responses=@(@{response=$initial},@{response=$(if($mode-eq'invalid'){$initial}else{$fixed})})
  @{nonce=$mode;responses=$responses}|ConvertTo-Json -Depth 45|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
  $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$service=$desktop.Child
  Await {Test-Path (Join-Path $root ready.json)} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
  Await {$s=Api '/api/activities/status';if($mode-eq'repaired'){@($s.recent|Where-Object {$_.kind-eq'wait'-and$_.status-eq'completed'}).Count-eq1}elseif($mode-eq'budget'){$s.planning_deferred_reason-and!$s.generating}else{$s.last_error-eq'activity_unavailable_kind'-and!$s.generating}} 'bounded repair outcome'
  $status=Api '/api/activities/status';$requests=@(Get-Content (Join-Path $root provider/requests.jsonl));$wire=$requests[-1]|ConvertFrom-Json -Depth 80
  if($mode-eq'budget'){Check ($requests.Count-eq1-and$status.queued-eq0-and$null-eq$status.active-and$status.recent.Count-eq0) 'repair cannot exceed the remaining model-call allowance'}
  else{
   Check ($requests.Count-eq2) 'one initial proposal and at most one repair call'
   $repair=$wire.messages[-1].content|ConvertFrom-Json -Depth 55;Check ($repair.error-eq'activity_unavailable_kind'-and$repair.unavailable_commands[0].kind-eq'move_to') 'repair points at the unavailable command instead of a vague failure'
   Check (@($repair.available_commands|Where-Object {$_-eq'move_to'}).Count-eq0) 'stay-here setting remains an actual execution boundary'
   if($mode-eq'repaired'){Check ($status.queued-eq0-and$null-eq$status.active-and$status.recent.Count-eq1-and$status.recent[0].kind-eq'wait') 'only the fully corrected plan executes, once'}
   else{Check ($status.queued-eq0-and$null-eq$status.active-and$status.recent.Count-eq0) 'a second invalid proposal never executes any initial partial plan'}
  }
  $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown';$desktop.Dispose();$desktop=$null;Stop-Process -Id $provider.Id;$provider=$null
 }
 @{completed=$true;checks=$checks;root=$base;paid_calls=0}|ConvertTo-Json|Set-Content (Join-Path $base acceptance.json) -Encoding utf8
 Write-Output "Planning repair passed: $checks checks. $base"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
