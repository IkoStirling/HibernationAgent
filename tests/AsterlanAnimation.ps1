param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
Add-Type -Path (Join-Path $PSScriptRoot NativeChatTest.cs)
Add-Type -Path (Join-Path $PSScriptRoot AnimationPixels.cs)
Add-Type -AssemblyName System.Drawing
$root=Join-Path $ProjectRoot ('artifacts/asterlan-animation-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root|Out-Null
$desktop=$null;$service=$null;$provider=$null;$checks=0
$env:HIBERNATION_SESSION_KEY='asterlan-animation-fixture'
$env:HIBERNATION_ANIMATION_KEY='synthetic-animation-fixture-key'
$env:TEMP=Join-Path $root tmp;$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP|Out-Null
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=15){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 25};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer asterlan-animation-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @p}
function Widget{(Api '/api/capabilities').plugins.Where({$_.id-eq'widget'})[0].current}
function Plugin($Name,$Arguments){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Wait-For {$script:receipt=Api '/api/plugin/result';$script:receipt.state-in@('succeeded','failed')} $Name;Check ($script:receipt.state-eq'succeeded') $Name}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_;delay_ms=500}})}|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Rest-Plan{Plan @(@{version=1;kind='call';text=$null;calls=@(@{id='lie-down';tool='widget.animate';args=@{state='resting';seconds=15}})},@{version=1;kind='reply';text='我先躺一会儿。';calls=@();presentation=@{speech=$true;bubble=$true}})}
function Send-Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text}}
function Compare-Pose([bool]$Mirror=$false){
 $w=Widget;$image=$desktop.CaptureClient($desktop.Find('HibernationDinosaurOverlay'));$left=[int][Math]::Floor($w.x*($image.Width-252));$top=[int][Math]::Floor($w.y*($image.Height-252)-$w.drag_lift_pixels)+36
 $bitmap=[Drawing.Bitmap]::new((Join-Path $ProjectRoot ('plugins/widget/characters/asterlan/'+$w.animation.displayed_image)))
 try{$lock=$bitmap.LockBits([Drawing.Rectangle]::new(0,0,168,168),[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb);try{$original=[byte[]]::new(168*168*4);[Runtime.InteropServices.Marshal]::Copy($lock.Scan0,$original,0,$original.Length)}finally{$bitmap.UnlockBits($lock)}}finally{$bitmap.Dispose()}
 $sample=[AnimationPixels]::Nearest($image.Bgra,$image.Width,$image.Height,$left,$top,252,$original,168,168,$Mirror)
 @{image=$w.animation.displayed_image;compared=$sample.Compared;mismatched=$sample.Mismatched;outside=[AnimationPixels]::Outside($image.Bgra,$image.Width,$image.Height,$left,$top,252)}
}
try{
 $provider=Start-Process (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Wait-For {Test-Path (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins
 $cfg.frontend=@{startupAnimation=$false};$cfg.profiles=@($cfg.profiles[0]);$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000
 $cfg.review=@{enabled=$false};$cfg.maxStartsPerHour=1000
 $cfg.profiles+=@{id='actor';label='Animation fixture';enabled=$true;protocol='chat-completions';origin=$fixture;basePath='/v1';model='fixture-model';credentialEnvironment='HIBERNATION_ANIMATION_KEY';timeoutMs=10000;maxOutputTokens=2048}
 $cfg.plugins=@{widget=@{enabled=$true;skin='lumen';roam=$false;reactionCacheEnabled=$false;size=252;respectSystemMotion=$false;animationMode='sequence';animationPack=(Join-Path $ProjectRoot plugins/widget/characters/asterlan);animationCacheMiB=16};tts=@{enabled=$true;backend='http';origin=$fixture;path='/v1/audio/speech';volume=0;maxSeconds=5;autoSpeak=$true}}
 $cfg|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root config.json) -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot)
 $service=$desktop.Child
 Wait-For {if($service.HasExited){throw 'fixture exited'};Test-Path (Join-Path $root ready.json)} 'ready'
 $script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
 Wait-For {(Widget).animation.ready-and(Widget).animation.pack-eq'asterlan-pixel'} 'Asterlan PNG loaded'
 $gallery=Api '/api/appearance';Check ($gallery.override_preset-eq'custom'-and@($gallery.skins|Where-Object id -eq asterlan).Count-eq1) 'dashboard discovers the installed preset independently from external pack overrides'
 $settings=(Api '/api/dashboard').settings;$settings.plugins.widget.skin='asterlan';$settings.plugins.widget.animationPack='';$settings.plugins.widget.animationMode='sequence'
 $null=Api '/api/settings' @{settings=$settings}
 Wait-For {(Widget).skin-eq'asterlan'-and(Widget).animation.ready} 'built-in Asterlan preset selected without external path'
 Check ((Widget).animation.pack-eq'asterlan-pixel') 'preset resolves to its own installed animation pack'
 $settings.plugins.widget.skin='lumen';$null=Api '/api/settings' @{settings=$settings}
 Wait-For {(Widget).skin-eq'lumen'-and(Widget).animation.ready} 'switch to legacy preset'
 Check ((Widget).animation.pack-ne'asterlan-pixel') 'legacy preset no longer retains the previous Asterlan override'
 $settings.plugins.widget.skin='asterlan';$null=Api '/api/settings' @{settings=$settings}
 Wait-For {(Widget).skin-eq'asterlan'-and(Widget).animation.ready} 'switch back to Asterlan'
 Check ((Widget).animation.rendered_pixels-gt100-and(Widget).animation.present_error-eq0) 'Asterlan native window presents actual PNG pixels'
 Check (!(Widget).reduced_motion) 'explicit widget setting allows animation independently of Windows UI motion'
 Check ((Widget).animation.image.StartsWith('frames/idle/south/')) 'normal idle uses supplied idle south rather than fatigue breathing'
 Check (@((Widget).animation_catalog|Where-Object {$_.state-eq'resting'-and$_.meaning.Contains('躺下')}).Count-eq1) 'model capability inventory explains the actual lying animation'
 $clickBefore=Widget;$clickWindow=$desktop.Find('HibernationDinosaurOverlay');$clickCapture=$desktop.CaptureClient($clickWindow);$cx=[int]($clickBefore.x*($clickCapture.Width-252)+126);$cy=[int]($clickBefore.y*($clickCapture.Height-252)+126)
 $null=[ChatWindowTest]::Send($clickWindow,0x201,0,($cy-shl16)-bor$cx);Start-Sleep -Milliseconds 250;$pressed=Widget
 Check ($pressed.pointer_pressed-and!$pressed.dragging-and!$pressed.dropping-and$pressed.drag_lift_pixels-eq0-and$pressed.state-eq$clickBefore.state) 'mouse down alone never lifts or changes the character into a drag pose'
 foreach($jitter in @(1,2,-1,0,2,0)){$null=[ChatWindowTest]::Send($clickWindow,0x200,1,(($cy+$jitter)-shl16)-bor($cx+$jitter))}
 $null=[ChatWindowTest]::Send($clickWindow,0x202,0,($cy-shl16)-bor$cx);$clicked=Widget
 Check (!$clicked.pointer_pressed-and!$clicked.dragging-and!$clicked.dropping-and$clicked.drag_lift_pixels-eq0-and$clicked.x-eq$clickBefore.x-and$clicked.y-eq$clickBefore.y) 'small nonaccumulating pointer jitter remains a click with no landing animation'
 $null=[ChatWindowTest]::Send($clickWindow,0x201,0,($cy-shl16)-bor$cx);$null=[ChatWindowTest]::Send($clickWindow,0x1F)
 Check (!(Widget).pointer_pressed-and!(Widget).dragging) 'lost capture clears a pending click without fabricating a drag'
 $null=Api '/api/control' @{paused=$true};Start-Sleep -Milliseconds 100;$scaled=Widget;$window=$desktop.Find('HibernationDinosaurOverlay');$capture=$desktop.CaptureClient($window)
 $left=[int]($scaled.x*($capture.Width-252));$top=[int]($scaled.y*($capture.Height-252))+36
 $bitmap=[Drawing.Bitmap]::new((Join-Path $ProjectRoot ('plugins/widget/characters/asterlan/'+$scaled.animation.displayed_image)))
 try{$lock=$bitmap.LockBits([Drawing.Rectangle]::new(0,0,168,168),[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb);try{$original=[byte[]]::new(168*168*4);[Runtime.InteropServices.Marshal]::Copy($lock.Scan0,$original,0,$original.Length)}finally{$bitmap.UnlockBits($lock)}}finally{$bitmap.Dispose()}
 $sample=[AnimationPixels]::Nearest($capture.Bgra,$capture.Width,$capture.Height,$left,$top,252,$original,168,168)
 Check ($scaled.size-eq252-and$sample.Compared-gt1000-and$sample.Mismatched-eq0) '1.5x native sprite preserves exact opaque source pixels with nearest neighbor sampling'
 $null=Api '/api/control' @{paused=$false}
 $initial=Widget;$target=[Math]::Min(.85,$initial.x+.13)
 Plugin 'widget.move' @{x=$target;y=$initial.y}
 Wait-For {(Widget).animation.phase-eq'start'-and(Widget).animation.resolved-eq'walk'} 'authored turn'
 $before=Widget;Start-Sleep -Milliseconds 250;$during=Widget
 @{before=$before;during=$during}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $root turn-observation.json)
 Check ($before.x-eq$during.x-and$before.y-eq$during.y-and$during.animation.frame-gt$before.animation.frame) 'turn frames advance while desktop position remains fixed'
 $turnPixels=@();$seen=@{}
 while((Widget).animation.phase-eq'start'){
  $null=Api '/api/control' @{paused=$true};Start-Sleep -Milliseconds 40;$frame=Widget
  if(!$seen.ContainsKey($frame.animation.frame)){$seen[$frame.animation.frame]=$true;$turnPixels+=Compare-Pose}
  $null=Api '/api/control' @{paused=$false};Start-Sleep -Milliseconds 65
 }
 $turnPixels|ConvertTo-Json -Depth 10|Set-Content (Join-Path $root turn-pixels.json)
 Check ($turnPixels.Count-ge5-and@($turnPixels|Where-Object {$_.mismatched-ne0-or$_.outside-ne0}).Count-eq0) 'turn PNG RGBA matches actual native frames with no extra face stripe or external white block'
 Wait-For {(Widget).animation.phase-eq'loop'-and(Widget).x-gt$initial.x+.001} 'right walk after turn'
 Check $true 'translation starts only after turn completes'
 Plugin 'widget.animate' @{state='idle'}
 Wait-For {(Widget).animation.phase-eq'end'} 'turn back to front'
 Wait-For {(Widget).animation.resolved-eq'idle'} 'front idle'
 Plugin 'widget.animate' @{state='resting'}
 Wait-For {(Widget).animation.resolved-eq'resting'-and(Widget).animation.phase-eq'start'} 'lying down'
 Wait-For {(Widget).animation.phase-eq'loop'-and(Widget).animation.resolved-eq'resting'} 'actual lying sleep pose'
 Check ((Widget).animation.variant-eq'lying-sleep') 'sleep uses authored lying state rather than closed-eyes standing'
 $sleepPosition=Widget
 Plugin 'widget.move' @{x=[Math]::Min(.85,$sleepPosition.x+.10);y=$sleepPosition.y}
 Wait-For {(Widget).animation.phase-eq'end'-and(Widget).animation.resolved-eq'resting'} 'wake before walking'
 Start-Sleep -Milliseconds 200
 Check ((Widget).x-eq$sleepPosition.x) 'wake-up finishes in place before movement'
 Wait-For {(Widget).animation.phase-eq'start'-and(Widget).animation.resolved-eq'walk'} 'turn follows wake'
 Check ((Widget).x-eq$sleepPosition.x) 'turn after wake also remains in place'
 Wait-For {(Widget).animation.phase-eq'loop'-and(Widget).x-gt$sleepPosition.x+.001} 'walking follows wake and turn'
 $window=$desktop.Find('HibernationDinosaurOverlay');$w=Widget
 $capture=$desktop.CaptureClient($window);$x=[int]($w.x*($capture.Width-252)+126);$y=[int]($w.y*($capture.Height-252)+126)
 $null=[ChatWindowTest]::Send($window,0x201,0,($y-shl16)-bor$x)
 $null=[ChatWindowTest]::Send($window,0x200,1,($y-shl16)-bor($x+10))
 Wait-For {$script:firstPickup=Widget;$script:firstPickup.animation.resolved-eq'dragged_side'-and$script:firstPickup.animation.phase-eq'start'} 'side pickup interrupts movement at its current facing'
 Check ($script:firstPickup.animation.frame-le2-and$script:firstPickup.animation.image-notmatch'/00[0-4]\.png$') 'side pickup starts sideways rather than jumping to a frontal pose'
 Check ((Widget).animation.ready-and(Widget).animation.procedural_fallback_paints-eq0) 'first cold drag displays the authored PNG without a green procedural flash'
 Start-Sleep -Milliseconds 150;$lifted=Widget
 Check ($lifted.drag_lift_pixels-ge30-and$lifted.y-eq$w.y) 'holding lifts visual position without changing saved ground position'
 Wait-For {(Widget).animation.resolved-eq'dragged_side'-and(Widget).animation.phase-eq'loop'} 'side lift turns smoothly into front suspension'
 $null=Api '/api/control' @{paused=$true};Start-Sleep -Milliseconds 50;$cleanDrag=Compare-Pose
 Check ($cleanDrag.mismatched-eq0-and$cleanDrag.outside-eq0) 'dragged sprite and transparent surroundings have no hover panel or stale pixels'
 $null=Api '/api/control' @{paused=$false}
 $null=[ChatWindowTest]::Send($window,0x200,1,($y-shl16)-bor($x+30))
 $null=[ChatWindowTest]::Send($window,0x202,0,($y-shl16)-bor($x+30))
 $falling=Widget
 Check ($falling.dropping-and$falling.drag_lift_pixels-gt0-and$falling.animation.resolved-eq'dragged_side') 'release falls before starting the authored landing sequence'
 Wait-For {$script:contact=Widget;$script:contact.animation.resolved-eq'landing'} 'landing starts'
 $contact=$script:contact;$contact|ConvertTo-Json -Depth 30|Set-Content (Join-Path $root contact-observation.json)
 Check ($contact.animation.ready-and$contact.animation.frame-le2-and$contact.animation.displayed_image-match'^frames/drag-drop/south/00[5-7]\.png$'-and$contact.animation.procedural_fallback_paints-eq0) 'contact immediately displays compression frames without standing lead-in or procedural replacement'
 Check ((Widget).drag_lift_pixels-eq0-and(Widget).y-eq$lifted.y) 'landing returns exactly to logical ground without accumulated offset'
 Start-Sleep -Milliseconds 650
 Check ((Widget).state-eq'landing'-and(Widget).animation.phase-eq'start'-and(Widget).animation.procedural_fallback_paints-eq0) 'contact and recovery play continuously without temporary procedural replacement'
 Wait-For {(Widget).state-eq'curious'} 'landing completes'
 $leftOrigin=Widget;Plugin 'widget.move' @{x=[Math]::Max(.02,$leftOrigin.x-.10);y=$leftOrigin.y}
 Wait-For {(Widget).animation.resolved-eq'walk'-and(Widget).animation.phase-eq'loop'-and(Widget).x-lt$leftOrigin.x-.001} 'left walk'
 $w=Widget;$capture=$desktop.CaptureClient($window);$x=[int]($w.x*($capture.Width-252)+126);$y=[int]($w.y*($capture.Height-252)+126)
 $null=[ChatWindowTest]::Send($window,0x201,0,($y-shl16)-bor$x)
 $null=[ChatWindowTest]::Send($window,0x200,1,($y-shl16)-bor($x-10))
 Wait-For {(Widget).animation.resolved-eq'dragged_side'} 'left side pickup'
 Wait-For {(Widget).animation.phase-eq'loop'} 'left pickup reaches front suspension'
 $null=Api '/api/control' @{paused=$true};Start-Sleep -Milliseconds 50;$leftPixels=Compare-Pose $true
 Check ($leftPixels.mismatched-eq0-and$leftPixels.outside-eq0) 'left pickup preserves facing and mirrors exact source pixels without stray panels'
 $null=Api '/api/control' @{paused=$false}
 $null=[ChatWindowTest]::Send($window,0x200,1,($y-shl16)-bor($x-30));$null=[ChatWindowTest]::Send($window,0x202,0,($y-shl16)-bor($x-30))
 Wait-For {(Widget).state-eq'curious'} 'left landing completes'
 $null=Api '/api/control' @{paused=$true};$frozen=Widget;Start-Sleep -Milliseconds 250;$still=Widget
 Check ($frozen.x-eq$still.x-and$frozen.animation.frame-eq$still.animation.frame) 'pause freezes Asterlan motion'
 $null=Api '/api/control' @{paused=$false}
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 40|ConvertFrom-Json -AsHashtable
 $settings.plugins.widget.reducedMotion=$true
 $null=Api '/api/settings' @{settings=$settings}
 Check ((Widget).reduced_motion) 'application reduced-motion preference still takes precedence'
 $settings.plugins.widget.reducedMotion=$false
 $null=Api '/api/settings' @{settings=$settings}
 Wait-For {(Widget).animation.ready-and!(Widget).reduced_motion} 'animation resumes after configuration reload'
 Check ((Widget).animation.failed_images-eq0-and(Widget).animation.cache_bytes-le(Widget).animation.cache_limit_bytes) 'all requested imported images decode within cache budget'
 $null=Api '/api/model' @{id='actor'}
 Rest-Plan;Send-Message '先躺下休息一会儿。'
 Wait-For {(Widget).pending_pose-eq'resting'} 'model lying intent queued during conversation'
 Check ((Widget).state-ne'resting') 'model tool receipt does not prematurely claim the pose is active'
 Wait-For {(Widget).speech_active} 'real silent fixture audio starts'
 Check ((Widget).pending_pose-eq'resting'-and(Widget).held_pose-eq'') 'reply audio cannot erase the pending lying intent'
 Wait-For {(Widget).animation.resolved-eq'resting'-and(Widget).animation.phase-eq'loop'} 'lying starts after audio and bubble finish' 20
 Check ((Widget).held_pose-eq'resting'-and!(Widget).speech_active) 'actual lying remains active after the model response'
 $capture=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 60
 Check ($capture.messages[0].content.Contains('animation_catalog')-and$capture.messages[0].content.Contains('疲劳')) 'actor receives available animation meanings and fatigue constraint'
 Plugin 'widget.animate' @{state='idle'}
 Wait-For {(Widget).animation.resolved-eq'idle'} 'wake to default idle'
 Plugin 'widget.animate' @{state='breathing';seconds=1}
 Wait-For {(Widget).animation.resolved-eq'breathing'} 'explicit fatigue pose'
 Wait-For {(Widget).animation.resolved-eq'idle'-and(Widget).held_pose-eq''} 'fatigue pose expires back to supplied idle'
 Rest-Plan;Send-Message '再排队躺下一次。'
 Wait-For {(Widget).speech_active-and!(Api '/api/chat').busy} 'second reply before deferred pose'
 Plan @(@{version=1;kind='silence';text=$null;calls=@()});Send-Message '先别躺下，听我说。'
 Wait-For {!(Api '/api/chat').busy-and(Widget).pending_pose-eq''} 'new input cancels queued pose'
 Start-Sleep -Milliseconds 3500
 Check ((Widget).pending_pose-eq''-and(Widget).held_pose-eq''-and(Widget).state-ne'resting') 'cancelled pose never replays after interrupted audio'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 40|ConvertFrom-Json -AsHashtable;$settings.plugins.widget.roam=$true;$settings.plugins.widget.roamPauseMinSeconds=20;$settings.plugins.widget.roamPauseMaxSeconds=50;$null=Api '/api/settings' @{settings=$settings}
 $w=Widget;Plugin 'widget.move' @{x=[Math]::Min(.9,$w.x+.002);y=$w.y}
 Wait-For {(Widget).state-eq'idle'} 'short move completes'
 $settled=Widget;Check ($settled.roam_pause_remaining_seconds-ge19-and$settled.roam_pause_remaining_seconds-le50) 'completed walk starts a full 20 to 50 second pause'
 Start-Sleep -Milliseconds 5500
 Check ((Widget).state-eq'idle'-and(Widget).x-eq$settled.x-and(Widget).y-eq$settled.y) 'roaming remains still beyond the former 2 to 5 second pause'
 Check ((Api '/api/chat').health-eq'ok') 'isolated application remains healthy'
 $null=Api '/api/shutdown' @{}
 Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'fixture exits cleanly'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json)
 Write-Output "Asterlan animation passed: $checks checks. $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited-and$service.Path-eq(Join-Path $BuildRoot hibernation-agent.exe)){Stop-Process -Id $service.Id}}}
 if($desktop){$desktop.Dispose()}
 if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
}
