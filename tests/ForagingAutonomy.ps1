param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$root=Join-Path $BuildRoot ('integration/foraging-autonomy-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$env:HIBERNATION_SESSION_KEY='isolated-foraging-autonomy';$desktop=$null;$service=$null;$provider=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=30){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if(& $Probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $Name"}
function Api($Path,$Body=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer isolated-foraging-autonomy'};TimeoutSec=10};if($null-ne$Body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Body|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plugin($Name,$Arguments=@{}){$id=[Guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments;request_id=$id};Await {$script:receipt=Api '/api/plugin/result';$receipt.request_id-eq$id-and$receipt.state-in@('succeeded','failed')} $Name;return $receipt.result}
function Current($Name){$v=@((Api '/api/capabilities').plugins|Where-Object id -eq $Name)[0].current;if($Name-eq'canteen'){return $v.current};$v}
function Requests{if(Test-Path (Join-Path $root provider/requests.jsonl)){return @(Get-Content (Join-Path $root provider/requests.jsonl)).Count};0}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'isolated host exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend ready';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown';$desktop.Dispose();$script:desktop=$null}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $root provider/port)} 'local provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false
 $cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.review=@{enabled=$false};$cfg.backgroundBudget=@{enabled=$false;autoEconomy=$true}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Local fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;reactionCacheEnabled=$false;showBubble=$false};habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false;localGatherProbability=1;localGatherCheckSeconds=5;localGatherCooldownSeconds=30};canteen=@{enabled=$true;automatic=$false};'deepseek-pricing'=@{enabled=$true;mode='force-economy';fixtureOrigin=$fixture}}
 $cfg|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='actor'};Check ((Api '/api/dashboard').deepseek_pricing.defer_prefetch) 'paid planning suppressed'
 $a=Plugin 'habitat.create' @{kind='berry';placement='nearby';lifetime_min_seconds=500;lifetime_max_seconds=500};Check $a.ok 'actual collectible created'
 $b=Plugin 'habitat.create' @{kind='berry';placement='nearby';lifetime_min_seconds=500;lifetime_max_seconds=500};Check $b.ok 'second target remains optional'
 Await {$script:pickup=Current 'widget';if($pickup.state-eq'collecting'){$pickup|Select-Object state,animation,visible,desktop|ConvertTo-Json -Depth 8|Set-Content (Join-Path $root pickup-probe.json)};$pickup.state-eq'collecting'-and$pickup.animation.requested-eq'collecting'-and$pickup.animation.resolved-eq'curious'-and$pickup.animation.held_item} 'pickup body state' 35
 $w=Current 'widget';Check ($w.animation.requested-eq'collecting'-and$w.animation.resolved-eq'curious'-and$w.animation.held_item) 'sequence pack reuses observation with actual held prop'
 Await {@((Plugin 'canteen.inventory').result.items|Where-Object {$_.ingredient_id-eq'berry'-and$_.container-eq'backpack'}).Count-eq1} 'real object enters shared inventory'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.source-eq'local-foraging'-and$_.status-eq'completed'}).Count-eq1} 'queue completes from gathering receipt'
 Check ((Current 'habitat').objects.Count-eq1) 'one admission does not collect the entire desktop'
 Check ((Requests)-eq0) 'local collection uses zero model requests'
 $cadence=(Api '/api/activities/status').local_foraging;Check ($cadence.next_ms-$cadence.last_checked_ms-eq30000) 'configured cooldown recorded'
 Stop-Backend;Start-Backend
 Check ((Api '/api/activities/status').local_foraging.next_ms-eq$cadence.next_ms) 'cooldown survives encrypted restart'
 Check (@((Plugin 'canteen.inventory').result.items|Where-Object ingredient_id -eq 'berry').Count-eq1) 'collected instance survives restart once'
 # A single background planning call fills the one-call background budget.
 # Foreground dialogue deliberately does not count against that budget.
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable
 $settings.plugins.'deepseek-pricing'.mode='force-normal';$settings.backgroundBudget=@{enabled=$true;autoEconomy=$true;maxCallsPerDay=1;maxInputTokensPerDay=500000}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{version=1;commands=@()}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/settings' @{settings=$settings}
 Await {(Requests)-ge1-and!(Api '/api/activities/status').generating} 'single background planning call'
 Check ((Requests)-eq1-and!(Api '/api/dashboard').idle_economy.optional_model_work_allowed) 'background reserve closes after one planning request'
 Await {@((Plugin 'canteen.inventory').result.items|Where-Object ingredient_id -eq 'berry').Count-eq2} 'reserve-blocked planning still collects locally' 45
 Check ((Requests)-eq1) 'reserve fallback makes no extra model call'
 Await {(Current 'widget').state-ne'collecting'-and!(Current 'widget').animation.held_item} 'pickup prop retires after receipt'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;real_paid_calls=0;endedUtc=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Foraging autonomy passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}

