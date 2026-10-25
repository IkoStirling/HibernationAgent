param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Parameter(Mandatory)][string]$FixtureVideo)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$checks=0;$service=$null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class OwnWindow {
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out IntPtr r);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect r);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 public static IntPtr Find(uint pid,string name){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var s=new StringBuilder(128);GetClassName(h,s,128);if(s.ToString()==name){result=h;return false;}}return true;},IntPtr.Zero);return result;}
 public static void Mouse(IntPtr h,uint m,int x,int y){IntPtr r;SendMessageTimeout(h,m,IntPtr.Zero,new IntPtr((y<<16)|(x&65535)),2,1000,out r);}
}
'@
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$a=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer desktop-media-fixture'};TimeoutSec=10};if($null-ne$Data){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @a}
function Plugin($Name,$ToolArguments){$null=Api '/api/plugin/test' @{tool=$Name;args=$ToolArguments};Wait-For {$script:pluginResult=Api '/api/plugin/result';$script:pluginResult.state-in@('succeeded','failed','paused')} $Name;return $script:pluginResult.result}
$root=Join-Path $ProjectRoot ('artifacts/desktop-media-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force "$root/data/materials"|Out-Null
$cfg=Get-Content "$ProjectRoot/config/example.json" -Raw|ConvertFrom-Json -AsHashtable;$cfg.maxStartsPerHour=1000;$cfg.dataRoot="$root/data";$cfg.characterRoot="$ProjectRoot/character";$cfg.uiRoot="$ProjectRoot/ui";$cfg.pluginRoot="$ProjectRoot/out/build/plugins";$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000;$cfg.plugins=@{widget=@{enabled=$true;roam=$false};tts=@{enabled=$true;backend='system';autoSpeak=$false;volume=0};television=@{enabled=$true;volume=0;wakeOnEnd=$false}}
$cfg|ConvertTo-Json -Depth 30|Set-Content "$root/config.json";Copy-Item -LiteralPath $FixtureVideo -Destination "$root/data/materials/clip.mp4"
$env:HIBERNATION_SESSION_KEY='desktop-media-fixture';$env:TEMP="$ProjectRoot/out/build/tmp";$env:TMP=$env:TEMP
try{
 $service=Start-Process "$ProjectRoot/out/build/hibernation-agent.exe" -ArgumentList ('--desktop --config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/service.log" -RedirectStandardError "$root/error.log"
 Wait-For {if($service.HasExited){throw 'desktop process exited'};Test-Path "$root/ready.json"} 'desktop ready';$ready=Get-Content "$root/ready.json" -Raw|ConvertFrom-Json;$script:origin='http://127.0.0.1:'+$ready.port
 $widget=[OwnWindow]::Find($service.Id,'HibernationDinosaurOverlay');Check ($widget-ne[IntPtr]::Zero) 'native widget belongs to isolated app process'
 $null=[OwnWindow]::SetThreadDpiAwarenessContext([IntPtr](-4));$screen=New-Object OwnWindow+Rect;$null=[OwnWindow]::GetClientRect($widget,[ref]$screen);$w=$screen.right;$h=$screen.bottom
 $before=Plugin 'widget.status' @{};$x=[int]($before.result.x*($w-112)+56);$y=[int]($before.result.y*($h-112)+56)
 $timer=[Diagnostics.Stopwatch]::StartNew();[OwnWindow]::Mouse($widget,0x201,$x,$y);[OwnWindow]::Mouse($widget,0x200,90,$y);[OwnWindow]::Mouse($widget,0x202,90,$y)
 Wait-For {$script:left=(Plugin 'speech.status' @{}).result;return $left.stereo_pan-lt-0.5} 'left audio direction';Check ($timer.ElapsedMilliseconds-lt1500-and$left.channel_left-gt$left.channel_right) 'drag updates actual speech direction promptly'
 [OwnWindow]::Mouse($widget,0x201,90,$y);[OwnWindow]::Mouse($widget,0x200,$w-90,$y);[OwnWindow]::Mouse($widget,0x202,$w-90,$y)
 Wait-For {$script:right=(Plugin 'speech.status' @{}).result;return $right.stereo_pan-gt0.5} 'right audio direction';Check ($right.channel_right-gt$right.channel_left) 'opposite screen edge reverses channel gains'
 $null=Plugin 'widget.show' @{visible=$false};Wait-For {[math]::Abs((Plugin 'speech.status' @{}).result.stereo_pan)-lt0.01} 'hidden widget centers audio';Check $true 'hidden widget centers audio'
 Start-Sleep -Milliseconds 300;Check ((Api '/api/chat').messages.Count-eq0-and(Plugin 'speech.status' @{}).result.started_count-eq0-and!(Api '/api/chat').busy) 'mouse gestures neither queue model work nor emit delayed speech'
 $opened=Plugin 'television.open' @{source='materials/clip.mp4'};Check $opened.ok 'native television opens using engine player';Wait-For {(Plugin 'television.observe' @{include_frame=$false}).result.frame_ready} 'native first frame'
 $tv=[OwnWindow]::Find($service.Id,'HibernationFloatingTelevision');Check ($tv-ne[IntPtr]::Zero) 'TV capture is restricted to the isolated app window'
 $rect=New-Object OwnWindow+Rect;$null=[OwnWindow]::GetClientRect($tv,[ref]$rect);$bitmap=New-Object Drawing.Bitmap($rect.right,$rect.bottom);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{for($i=0;$i-lt24;$i++){$dc=$graphics.GetHdc();try{Check ([OwnWindow]::PrintWindow($tv,$dc,1)) 'complete client frame is available'}finally{$graphics.ReleaseHdc($dc)};$pixel=$bitmap.GetPixel([int]($rect.right/2),[int](($rect.bottom-34)/2));Check (!($pixel.R-eq24-and$pixel.G-eq29-and$pixel.B-eq25)) 'video center has no background-only repaint';if($i-eq5){$bitmap.Save("$root/television.png")};Start-Sleep -Milliseconds 25}}finally{$graphics.Dispose();$bitmap.Dispose()}
 $observation=(Plugin 'television.observe' @{include_frame=$false}).result;Check ($observation.playback_engine-eq'AYVideoPlayer'-and$observation.sync_source-eq'AudioMaster') 'native video uses the engine audio-master clock'
 $settings=(Api '/api/dashboard').settings;foreach($id in @('television','widget','tts')){$settings.plugins.$id.enabled=$false};$null=Api '/api/settings' @{settings=$settings};Check (!$service.HasExited-and(Api '/api/dashboard').plugins.Where({$_.enabled}).Count-eq0) 'all media plugins unload without losing the core'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(8000)-and$service.ExitCode-eq0) 'native media process shuts down cleanly'
 @{completed=$true;checks=$checks;root=$root;timestamp=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content "$root/acceptance.json"
 Write-Output "Desktop media integration passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}}
