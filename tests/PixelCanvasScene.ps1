param([Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$desktop=$null;$service=$null;$checks=0
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
Add-Type -Path (Join-Path $ProjectRoot tests/NativeChatTest.cs)
$root=Join-Path $ScratchRoot ('pixel-scene-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='pixel-scene-synthetic'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer pixel-scene-synthetic'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Surface{@((Api '/api/capabilities').plugins|Where-Object id -eq 'pixel-canvas')[0].current.surface}
function Tool($Name,$Arguments=@{}){
 $request=[Guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments;request_id=$request}
 Await {$candidate=Api '/api/plugin/result';if($candidate.request_id-eq$request-and$candidate.state-in@('succeeded','failed','paused')){$script:receipt=$candidate.result;return $true};$false} $Name
 return $script:receipt
}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable}
function StartBackend{
 $script:desktop=[PrivateDesktop]::new((Join-Path $runtime 'hibernation-agent.exe'),$configPath,$ready,$ProjectRoot);$script:service=$script:desktop.Child
 Await {$script:service.Refresh();if($script:service.HasExited){throw 'own inactive desktop host exited'};if(Test-Path -LiteralPath $ready){$value=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json;if($value.pid-eq$script:service.Id){$script:origin='http://127.0.0.1:'+$value.port;return $true}};$false} 'private host'
}
function StopBackend{$null=Api '/api/shutdown' @{};Check ($script:service.WaitForExit(15000)-and$script:service.ExitCode-eq0) 'private desktop host saves and shuts down';$script:desktop.Dispose();$script:desktop=$null;$script:service=$null}
function Checkpoint($Visible){Await {& (Join-Path $runtime Hibernation_PixelCheckpointProbe.exe) $cfg.dataRoot $Visible;return $LASTEXITCODE-eq0} ('durable surface '+$Visible)}
function CrashBackend{$script:service.Kill($true);Check ($script:service.WaitForExit(15000)) 'only the fixture host tree is terminated';$script:desktop.Dispose();$script:desktop=$null;$script:service=$null}
try{
 $runtime=Join-Path $root runtime;New-Item -ItemType Directory -Force $runtime|Out-Null
 Copy-Item -LiteralPath (Join-Path $BuildRoot hibernation-agent.exe) -Destination $runtime
 Copy-Item -LiteralPath (Join-Path $BuildRoot Hibernation_PixelCheckpointProbe.exe) -Destination $runtime
 Get-ChildItem -LiteralPath $BuildRoot -File -Filter '*.dll'|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $runtime}
 $plugins=Join-Path $root plugins;New-Item -ItemType Directory -Force $plugins|Out-Null
 Copy-Item -LiteralPath (Join-Path $BuildRoot plugins/pixel-canvas) -Destination $plugins -Recurse
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.draftsRoot=Join-Path $root drafts;$cfg.materialsRoot=Join-Path $root materials
 New-Item -ItemType Directory -Force $cfg.dataRoot|Out-Null
 [IO.File]::WriteAllText((Join-Path $cfg.dataRoot pixel-scene-fixture),'hibernation.pixel-scene.synthetic.v1')
 $cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=$plugins
 $cfg.plugins=@{'pixel-canvas'=@{enabled=$true;reducedMotion=$true;restoreDesktopArtwork=$true}};$cfg.review=@{enabled=$false;profileId='';maxRewrites=1};$cfg.computer=@{enabled=$false};$cfg.frontend.startupAnimation=$false
 $cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8
 StartBackend
 Check ((Test-Path -LiteralPath $cfg.draftsRoot)-and(Test-Path -LiteralPath $cfg.materialsRoot)) 'fresh explicitly configured workspace roots are created before services mount them'
 $created=Tool 'pixel.create' @{id='keepsake';title='桌边的小灯';width=16;height=16};Check ($created.ok-and$created.result.document.revision-eq1) 'native plugin authors a real document in a desktop process'
 $edited=Tool 'pixel.edit' @{id='keepsake';expected_revision=1;frame=0;commands=@(@{op='rect';x=5;y=3;x2=10;y2=9;color=8;filled=$true})};Check $edited.ok 'drawing is actually written before display'
 $shown=Tool 'pixel.show' @{id='keepsake';visible=$true;scale=4};Check ($shown.ok-and$shown.result.accepted-and!$shown.result.completed) 'tool receipt is acceptance rather than premature display completion'
 Await {(Surface).visible-and(Surface).revision-eq2} 'actual display'
 $window=$desktop.Find('HibernationPixelArtworkV1');Check ($window-ne[IntPtr]::Zero-and[ChatWindowTest]::IsWindowVisible($window)) 'published display state corresponds to a real owned HWND'
 Check (!(Surface|ConvertTo-Json -Depth 10).Contains('pixels')-and!(Surface|ConvertTo-Json -Depth 10).Contains('workspace_root')) 'role surface metadata excludes pixel grids and private workspace paths'
 $null=[ChatWindowTest]::Send($window,0x10);Await {!(Surface).visible} 'window close'
 $reopen=Api '/api/surfaces/show' @{id='pixel-canvas'};Check ($reopen.accepted-and!$reopen.completed) 'common reopening route reports a queued request'
 Await {(Surface).visible} 'reopened artwork'
 $window=$desktop.Find('HibernationPixelArtworkV1');$null=[ChatWindowTest]::Send($window,0x10);Await {!(Surface).visible} 'close before fixture crash';Checkpoint 'false';CrashBackend;StartBackend
 Check (!(Surface).visible-and!(Surface).pending) 'confirmed hidden artwork is not resurrected after a process crash'
 $null=Tool 'pixel.show' @{id='keepsake';visible=$true};Await {(Surface).visible} 'show before fixture crash';Checkpoint 'true';CrashBackend;StartBackend;Await {(Surface).visible} 'visible checkpoint restores after crash'
 Check ($desktop.Find('HibernationPixelArtworkV1')-ne[IntPtr]::Zero) 'confirmed visible artwork survives crash with a real restored HWND'
 StopBackend;StartBackend;Await {(Surface).visible-and(Surface).document_id-eq'keepsake'} 'restore actual previous artwork'
 Check ($desktop.Find('HibernationPixelArtworkV1')-ne[IntPtr]::Zero) 'restart restores a validated document to a real desktop window'
 $settings=Settings;$settings.plugins.'pixel-canvas'.restoreDesktopArtwork=$false;$null=Api '/api/settings' @{settings=$settings}
 StopBackend;StartBackend;Check (!(Surface).visible-and!(Surface).pending) 'restart restore can be explicitly disabled'
 $null=Tool 'pixel.show' @{id='keepsake';visible=$true};Await {(Surface).visible} 'show for file-removal edge'
 $documentFile=Join-Path $cfg.draftsRoot 'pixel-canvas/documents/keepsake.json';$saved=[IO.File]::ReadAllBytes($documentFile)
 # Delete only this fixture's explicitly named synthetic document, no recursion.
 Remove-Item -LiteralPath $documentFile
 $hidden=Tool 'pixel.show' @{id='keepsake';visible=$false};Check $hidden.ok 'owned artwork can still be hidden when its source file was removed'
 Await {!(Surface).visible} 'hide from cache'
 [IO.File]::WriteAllBytes($documentFile,$saved)
 $null=Tool 'pixel.show' @{id='keepsake';visible=$true};Await {(Surface).visible} 'restore synthetic source'
 $newDrafts=Join-Path $root another-drafts;$settings=Settings;$settings.draftsRoot=$newDrafts;$null=Api '/api/settings' @{settings=$settings}
 Await {!(Surface).visible-and(Surface).document_id-eq''} 'workspace change clears previous display'
 $rejected=$false;try{$null=Api '/api/plugin/test' @{tool='pixel.list';args=@{};request_id=[Guid]::NewGuid().ToString('N');expected_workspace=$cfg.draftsRoot}}catch{$rejected=$_.Exception.Response.StatusCode-eq409}
 Check $rejected 'stale operator workspace identity is rejected before tool enqueue'
 $null=Api '/api/surfaces/show' @{id='pixel-canvas'};Check (!(Surface).visible-and(Surface).document_id-eq'') 'new workspace cannot reopen artwork from the previous root'
 Check ((Api '/api/dashboard').model_budget.used-eq0-and(Api '/api/chat').messages.Count-eq0) 'operator display and restoration add no model calls or fictional dialogue'
 StopBackend
 @{completed=$true;checks=$checks;inactive_desktop=$true;api_calls=0;private_data_used=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json)
 Write-Output "Pixel desktop scene passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()}}
