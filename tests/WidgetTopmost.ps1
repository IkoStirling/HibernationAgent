param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('integration/widget-topmost-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs);$env:HIBERNATION_SESSION_KEY='widget-layer-fixture';$desktop=$null;$service=$null;$checks=0
function Check($v,$name){if(!$v){throw "FAILED: $name"};$script:checks++;Write-Output "PASS $name"}
function Await([scriptblock]$probe,$name){$end=[DateTime]::UtcNow.AddSeconds(15);while([DateTime]::UtcNow-lt$end){if(&$probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $name"}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer widget-layer-fixture'}};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @p}
function Widget{((Api '/api/capabilities').plugins|Where-Object id -eq widget).current}
function Show-Widget($visible){$key=[guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{tool='widget.show';args=@{visible=$visible};request_id=$key};Await {$r=Api '/api/plugin/result';$r.request_id-eq$key-and$r.state-eq'succeeded'} 'show request'}
try{
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.review=@{enabled=$false};$cfg.profiles=@($cfg.profiles[0]);$cfg.frontend.startupAnimation=$false;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';roam=$false;autonomousMode=$false}};$cfg|ConvertTo-Json -Depth 45|Set-Content (Join-Path $root config.json) -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$service=$desktop.Child
 Await {if($service.HasExited){throw 'fixture host exited'};Test-Path (Join-Path $root ready.json)} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
 Await {(Widget).native_topmost} 'initial native topmost';$window=$desktop.Find('HibernationDinosaurOverlay');Check ($window-ne[IntPtr]::Zero-and$desktop.IsTopmost($window)) 'actual overlay begins in topmost band'
 $before=Widget;$desktop.DemoteOwnedOverlay($window)
 Await {$w=Widget;$w.native_topmost-and$w.topmost_repairs-gt$before.topmost_repairs} 'native demotion repaired'
 Check ($desktop.IsTopmost($window)) 'OS confirms repaired topmost flag'
 $quiet=(Widget).topmost_repairs;Start-Sleep -Milliseconds 2300;Check ((Widget).topmost_repairs-eq$quiet) 'stable layer does not repeatedly raise window'
 $null=Api '/api/control' @{paused=$true};$desktop.DemoteOwnedOverlay($window);Await {$desktop.IsTopmost($window)} 'paused visible pet retains layer';$null=Api '/api/control' @{paused=$false}
 Show-Widget $false;Await {!(Widget).visible} 'hidden';$hidden=(Widget).topmost_repairs;Start-Sleep -Milliseconds 1200;Check (!(Widget).visible-and(Widget).topmost_repairs-eq$hidden) 'hidden overlay is not restored against user intent'
 Show-Widget $true;Await {(Widget).visible-and(Widget).native_topmost} 'show restores layer';Check ((Widget).topmost_error-eq0) 'no layer API errors'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown'
 @{completed=$true;checks=$checks;root=$root;user_windows_modified=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Widget layer regression passed: $checks checks. $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()}}
