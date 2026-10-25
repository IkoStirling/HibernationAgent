param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot 'out/build'};$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot 'NativeChatTest.cs')
$checks=0;$service=$null
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=20){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 35};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$a=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer animation-fixture'};TimeoutSec=10};if($null-ne$Data){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 35 -Compress))};Invoke-RestMethod @a}
function Widget{(Api '/api/capabilities').plugins.Where({$_.id-eq'widget'})[0].current}
function Plugin($Name,$Arguments){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Wait-For {$script:pluginResult=Api '/api/plugin/result';$script:pluginResult.state-in@('succeeded','failed','paused')} $Name;return $script:pluginResult.result.result}
function Save-Widget($Skin,$Palette,$Reduced=$false){$settings=(Api '/api/dashboard').settings;$settings.plugins.widget.skin=$Skin;$settings.plugins.widget.palette=$Palette;$settings.plugins.widget.reducedMotion=$Reduced;$null=Api '/api/settings' @{settings=$settings}}
$root=Join-Path $ProjectRoot ('artifacts/widget-animation-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:HIBERNATION_SESSION_KEY='animation-fixture';$env:TEMP="$BuildRoot/tmp";$env:TMP=$env:TEMP
try{
 $cfg=Get-Content "$ProjectRoot/config/example.json" -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot="$root/data";$cfg.characterRoot="$ProjectRoot/character";$cfg.uiRoot="$ProjectRoot/ui";$cfg.pluginRoot="$BuildRoot/plugins";$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000;$cfg.plugins=@{widget=@{enabled=$true;roam=$false;skin='dinosaur';palette='forest';reducedMotion=$false}}
 $cfg|ConvertTo-Json -Depth 35|Set-Content "$root/config.json" -Encoding utf8
 $service=Start-Process "$BuildRoot/hibernation-agent.exe" -ArgumentList ('--desktop --config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/service.log" -RedirectStandardError "$root/error.log"
 Wait-For {if($service.HasExited){throw 'desktop exited'};Test-Path "$root/ready.json"} 'desktop';$script:origin='http://127.0.0.1:'+((Get-Content "$root/ready.json" -Raw|ConvertFrom-Json).port)
 $widget=[ChatWindowTest]::Find($service.Id,'HibernationDinosaurOverlay');$main=[ChatWindowTest]::Find($service.Id,'HibernationConversationV2');$null=[ChatWindowTest]::Send($main,0x10)
 Check ((Widget).skin-eq'dinosaur') 'default dinosaur is preserved'
 foreach($skin in @('dinosaur','moonbird','lumen')){foreach($palette in @('forest','mist','amber')){Save-Widget $skin $palette;Check ((Widget).skin-eq$skin-and(Widget).palette-eq$palette) "appearance hot configuration $skin / $palette"}}
 $settings=(Api '/api/dashboard').settings;$settings.plugins.widget.skin='../outside';$rejected=$false;try{$null=Api '/api/settings' @{settings=$settings}}catch{$rejected=$true};Check ($rejected-and(Widget).skin-eq'lumen') 'invalid appearance preserves current plugin'
 Save-Widget 'dinosaur' 'forest' $true;Check (Widget).reduced_motion 'reduced motion setting applies live';Save-Widget 'dinosaur' 'forest'
 $phase=(Widget).animation_phase;Wait-For {(Widget).animation_phase-ne$phase} 'animation advances';Check $true 'idle animation clock advances'
 $before=Widget;$target=[math]::Min(.8,$before.x+.05);$null=Plugin 'widget.move' @{x=$target;y=$before.y};Start-Sleep -Milliseconds 200;$moving=Widget;Check ($moving.x-gt$before.x-and$moving.x-lt$target) 'movement approaches target without teleporting'
 $null=Api '/api/control' @{paused=$true};$paused=Widget;Start-Sleep -Milliseconds 600;$still=Widget;Check ($still.animation_phase-eq$paused.animation_phase-and$still.x-eq$paused.x-and$still.y-eq$paused.y) 'pause freezes animation clock and movement';$null=Api '/api/control' @{paused=$false};Wait-For {[math]::Abs((Widget).x-$target)-lt.002} 'movement resumes';Check $true 'resume continues from current position'
 [ChatWindowTest]::UseWindowCoordinates($widget);$screen=New-Object ChatWindowTest+Rect;$null=[ChatWindowTest]::GetClientRect($widget,[ref]$screen);$position=Widget;$x=[int]($position.x*($screen.right-112)+56);$y=[int]($position.y*($screen.bottom-112)+56)
 $null=[ChatWindowTest]::Send($widget,0x201,0,($y-shl16)-bor$x);Check ((Widget).state-eq'dragged') 'pointer down starts drag pose';$null=[ChatWindowTest]::Send($widget,0x1F);Check ((Widget).state-ne'dragged') 'cancelled capture never leaves widget stuck'
 $null=[ChatWindowTest]::Send($widget,0x201,0,($y-shl16)-bor$x);$null=[ChatWindowTest]::Send($widget,0x200,1,($y-shl16)-bor($x+30));$null=[ChatWindowTest]::Send($widget,0x202,0,($y-shl16)-bor($x+30));Check ((Widget).state-in@('landing','curious')) 'drag release has landing recovery';Wait-For {(Widget).state-eq'curious'} 'landing settles';Check $true 'landing returns to curiosity'
 Check ((Api '/api/chat').messages.Count-eq0-and!(Api '/api/chat').busy) 'animation and skins do not generate dialogue or model work'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(8000)-and$service.ExitCode-eq0) 'animation process closes cleanly'
 @{completed=$true;checks=$checks;root=$root;timestamp=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content "$root/acceptance.json";Write-Output "Widget animation passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}}
