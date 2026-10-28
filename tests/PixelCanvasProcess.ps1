param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$checks=0
$root=Join-Path $ScratchRoot ('pixel-process-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='pixel-process-synthetic-session'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer pixel-process-synthetic-session'};TimeoutSec=20};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Tool($Name,$Arguments=@{}){$request=[Guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments;request_id=$request};$script:toolResult=$null;Await {$candidate=Api '/api/plugin/result';if($candidate.request_id-eq$request-and$candidate.state-in@('succeeded','failed','paused')){$script:toolResult=$candidate.result;return $true};$false} $Name;return $script:toolResult}
function StartBackend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {$script:service.Refresh();if($script:service.HasExited){throw 'isolated backend exited'};if(Test-Path -LiteralPath $ready){try{$v=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json;if($v.pid-eq$script:service.Id){$script:origin='http://127.0.0.1:'+$v.port;return $true}}catch{}};$false} 'backend ready'
}
function StopBackend{$null=Api '/api/shutdown' @{};Check ($script:service.WaitForExit(15000)-and$script:service.ExitCode-eq0) 'backend saves and exits cleanly';$script:service=$null}
function PluginEnabled($Enabled){$settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$settings.plugins.'pixel-canvas'.enabled=$Enabled;$null=Api '/api/settings' @{settings=$settings}}
try{
 $pluginRoot=Join-Path $root plugins;New-Item -ItemType Directory -Force $pluginRoot|Out-Null
 Copy-Item -LiteralPath (Join-Path (Split-Path $Executable) plugins/pixel-canvas) -Destination $pluginRoot -Recurse
 # Run a private runtime copy so another lane can relink the candidate while
 # this separate process owns its executable and loaded runtime libraries.
 $runtimeCopy=Join-Path $root runtime;New-Item -ItemType Directory -Force $runtimeCopy|Out-Null
 Copy-Item -LiteralPath $Executable -Destination $runtimeCopy
 Get-ChildItem -LiteralPath (Split-Path $Executable) -File -Filter '*.dll'|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $runtimeCopy}
 $Executable=Join-Path $runtimeCopy 'hibernation-agent.exe'
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.draftsRoot=Join-Path $root drafts;$cfg.materialsRoot=Join-Path $root materials;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=$pluginRoot
 New-Item -ItemType Directory -Force $cfg.draftsRoot,$cfg.materialsRoot|Out-Null
 $cfg.plugins=@{'pixel-canvas'=@{enabled=$true;maximumDocuments=4;defaultScale=3}};$cfg.review=@{enabled=$false;profileId='';maxRewrites=1};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8
 StartBackend
 $initial=Api '/api/dashboard';Check (@($initial.plugins|Where-Object id -eq 'pixel-canvas').Count-eq1-and!$initial.plugin_failures.'pixel-canvas') 'native pixel plugin loaded through normal host'
 $listed=Tool 'pixel.list';Check ($listed.ok-and$listed.result.documents.Count-eq0) 'fresh isolated workshop starts empty'
 $created=Tool 'pixel.create' @{id='lamp';title='一盏小灯';width=16;height=16};Check ($created.ok-and$created.result.document.revision-eq1) 'operator creates actual draft'
 $edited=Tool 'pixel.edit' @{id='lamp';expected_revision=1;frame=0;commands=@(@{op='rect';x=4;y=4;x2=11;y2=11;color=8;filled=$true},@{op='pixel';x=7;y=3;color=1})};Check ($edited.ok-and$edited.result.document.revision-eq2-and$edited.result.document.frames[0].pixels[68]-eq8) 'host validates and executes drawing batch'
 $failed=Tool 'pixel.edit' @{id='lamp';expected_revision=2;frame=0;commands=@(@{op='pixel';x=0;y=0;color=5},@{op='pixel';x=17;y=0;color=2})};$unchanged=Tool 'pixel.read' @{id='lamp'}
 Check (!$failed.ok-and$failed.error.code-eq'pixel_invalid_command'-and$unchanged.result.document.revision-eq2-and$unchanged.result.document.frames[0].pixels[0]-eq0) 'out of canvas command rejects entire persisted batch'
 $failed=Tool 'pixel.edit' @{id='lamp';expected_revision=1;frame=0;commands=@(@{op='clear'})};Check (!$failed.ok-and$failed.error.code-eq'pixel_revision_conflict') 'stale editing revision returns actionable error'
 $animated=Tool 'pixel.frame' @{id='lamp';expected_revision=2;action='copy';index=0;duration_ms=350};Check ($animated.ok-and$animated.result.document.revision-eq3-and$animated.result.document.frames.Count-eq2) 'frame copying and timing are persisted'
 $preview=Tool 'pixel.preview' @{id='lamp';frame=0};$pngBytes=[Convert]::FromBase64String($preview.result.image.base64)
 Check ($preview.ok-and$preview.result.image.mime_type-eq'image/png'-and$pngBytes[0]-eq137-and$pngBytes[1]-eq80-and$preview.result.width-eq256) 'real PNG preview reaches operator with dimensions'
 Add-Type -AssemblyName System.Drawing
 $stream=[IO.MemoryStream]::new($pngBytes,$false);$bitmap=[Drawing.Bitmap]::new($stream)
 try{Check ($bitmap.Width-eq256-and$bitmap.Height-eq256-and$bitmap.GetPixel(0,0).A-eq0-and$bitmap.GetPixel(4*16,4*16).R-eq224) 'preview decodes with nearest-neighbor palette and alpha'}finally{$bitmap.Dispose();$stream.Dispose()}
 $export=Tool 'pixel.export' @{id='lamp';expected_revision=3;format='sheet'};Check ($export.ok-and(Test-Path -LiteralPath $export.result.path)-and(Test-Path -LiteralPath $export.result.manifest_path)) 'animation exports actual PNG and manifest files'
 Check ($export.result.relative_path-eq'pixel-canvas/exports/lamp-r3-sheet.png'-and[IO.Path]::GetFullPath($export.result.path).StartsWith([IO.Path]::GetFullPath($cfg.draftsRoot)+[IO.Path]::DirectorySeparatorChar)) 'export stays in configured draft workspace'
 $manifest=Get-Content -LiteralPath $export.result.manifest_path -Raw|ConvertFrom-Json;Check ($manifest.frames.Count-eq2-and$manifest.frames[1].duration_ms-eq350-and$manifest.frames[1].x-eq16) 'animation manifest matches real frame positions and duration'
 $show=Tool 'pixel.show' @{id='lamp';visible=$true;scale=3};Check (!$show.ok-and$show.error.code-eq'desktop_required') 'headless host does not pretend desktop display succeeded'
 $invalid=Tool 'pixel.read' @{id='../outside'};Check (!$invalid.ok-and$invalid.error.code-eq'pixel_identity') 'traversal identity cannot read another file'
 $null=Api '/api/control' @{paused=$true};$rejected=$false;try{$null=Api '/api/plugin/test' @{tool='pixel.list';args=@{};request_id=[Guid]::NewGuid().ToString('N')}}catch{$rejected=$_.Exception.Response.StatusCode.value__-eq409};Check $rejected 'paused application rejects operator action before plugin execution';$null=Api '/api/control' @{paused=$false}
 PluginEnabled $false;Check (@((Api '/api/dashboard').plugins|Where-Object {$_.id-eq'pixel-canvas'-and$_.enabled}).Count-eq0) 'hot disable unloads plugin';PluginEnabled $true
 $retained=Tool 'pixel.read' @{id='lamp'};Check ($retained.ok-and$retained.result.document.revision-eq3) 'hot reenable preserves previous document'
 Check ((Api '/api/dashboard').model_budget.used-eq0) 'operator drawing and export make no model requests'
 StopBackend;StartBackend
 $recovered=Tool 'pixel.read' @{id='lamp'};Check ($recovered.ok-and$recovered.result.document.revision-eq3-and$recovered.result.document.frames.Count-eq2) 'independent process restart restores document and animation'
 $undo=Tool 'pixel.undo' @{id='lamp';expected_revision=3};Check ($undo.ok-and$undo.result.document.revision-eq4-and$undo.result.document.frames.Count-eq1) 'undo history survives process restart with monotonic revision'
 $palette=Tool 'pixel.palette' @{id='lamp';expected_revision=4;index=8;color='#c0a070ff'};Check ($palette.ok-and$palette.result.document.revision-eq5-and$palette.result.document.palette[8]-eq'#C0A070FF'-and$palette.result.document.frames[0].pixels[68]-eq8) 'palette editing updates actual draft without changing pixel indices'
 $paletteFailure=Tool 'pixel.palette' @{id='lamp';expected_revision=5;index=0;color='#111111FF'};$still=Tool 'pixel.read' @{id='lamp'};Check (!$paletteFailure.ok-and$paletteFailure.error.code-eq'pixel_palette'-and$still.result.document.revision-eq5) 'transparent palette index cannot become opaque'
 $colorPreview=Tool 'pixel.preview' @{id='lamp';frame=0};$colorStream=[IO.MemoryStream]::new([Convert]::FromBase64String($colorPreview.result.image.base64),$false);$colorBitmap=[Drawing.Bitmap]::new($colorStream)
 try{Check ($colorBitmap.GetPixel(4*16,4*16).R-eq192) 'preview image reflects changed palette color'}finally{$colorBitmap.Dispose();$colorStream.Dispose()}
 $appended=Tool 'pixel.palette' @{id='lamp';expected_revision=5;index=24;color='#65B0D080'};Check ($appended.ok-and$appended.result.document.revision-eq6-and$appended.result.document.palette.Count-eq25) 'palette append adds an indexed color at the current boundary'
 $paletteUndo=Tool 'pixel.undo' @{id='lamp';expected_revision=6};Check ($paletteUndo.ok-and$paletteUndo.result.document.revision-eq7-and$paletteUndo.result.document.palette.Count-eq24) 'palette changes use the same persistent undo history'
 $paletteRedo=Tool 'pixel.redo' @{id='lamp';expected_revision=7};Check ($paletteRedo.ok-and$paletteRedo.result.document.revision-eq8-and$paletteRedo.result.document.palette.Count-eq25) 'redo restores the undone palette through the actual host'
 $null=Tool 'pixel.undo' @{id='lamp';expected_revision=8};$branch=Tool 'pixel.palette' @{id='lamp';expected_revision=9;index=8;color='#506070FF'};$noRedo=Tool 'pixel.redo' @{id='lamp';expected_revision=10};Check ($branch.ok-and!$noRedo.ok-and$noRedo.error.code-eq'pixel_redo_empty') 'new editor action invalidates prior redo branch'
 $alternateDrafts=Join-Path $root alternate-drafts;New-Item -ItemType Directory -Force $alternateDrafts|Out-Null
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$settings.draftsRoot=$alternateDrafts;$null=Api '/api/settings' @{settings=$settings}
 $missing=Tool 'pixel.read' @{id='lamp'};$newList=Tool 'pixel.list';Check (!$missing.ok-and$missing.error.code-eq'pixel_document_missing'-and$newList.result.documents.Count-eq0) 'hot workspace change removes access to old drafts'
 $newPiece=Tool 'pixel.create' @{id='other';title='别处的作品';width=16;height=16};Check ($newPiece.ok-and(Test-Path -LiteralPath (Join-Path $alternateDrafts 'pixel-canvas/documents/other.json'))) 'new draft is created only in newly configured root'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$settings.draftsRoot=$cfg.draftsRoot;$null=Api '/api/settings' @{settings=$settings}
 $original=Tool 'pixel.read' @{id='lamp'};$other=Tool 'pixel.read' @{id='other'};Check ($original.ok-and$original.result.document.revision-eq10-and!$other.ok) 'returning to original workspace restores only its own drafts'
 StopBackend
 @{completed=$true;checks=$checks;root=$root;actual_api_calls=0;private_production_data_used=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Pixel process passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
