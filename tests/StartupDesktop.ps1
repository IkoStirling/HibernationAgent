param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot 'out/build'};$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot 'NativeChatTest.cs');Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class StartupGeometry {
 [StructLayout(LayoutKind.Sequential)]struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]struct Monitor{public int size;public Rect area,work;public uint flags;}
 [DllImport("user32.dll")]static extern IntPtr MonitorFromWindow(IntPtr h,uint flags);
 [DllImport("user32.dll")]static extern bool GetMonitorInfo(IntPtr h,ref Monitor value);
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out Rect value);
 [DllImport("user32.dll")]static extern bool GetLayeredWindowAttributes(IntPtr h,out uint color,out byte alpha,out uint flags);
 public static int Alpha(IntPtr h){uint c,f;byte a;return GetLayeredWindowAttributes(h,out c,out a,out f)?a:255;}
 public static int[] Measure(IntPtr h){Rect r;GetWindowRect(h,out r);var m=new Monitor{size=Marshal.SizeOf<Monitor>()};GetMonitorInfo(MonitorFromWindow(h,2),ref m);return new[]{(r.left+r.right-m.work.left-m.work.right)/2,(r.top+r.bottom-m.work.top-m.work.bottom)/2,r.right-r.left,r.bottom-r.top};}
}
'@
$root=Join-Path $ProjectRoot ('artifacts/startup-desktop-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null;$service=$null;$checks=0
$env:HIBERNATION_SESSION_KEY='startup-desktop-fixture';$env:TEMP="$BuildRoot/tmp";$env:TMP=$env:TEMP
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=10){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 20};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$a=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer startup-desktop-fixture'};TimeoutSec=10};if($null-ne$Data){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @a}
function Start-App($Name,$Enabled,$Reduced){
 $script:activeRoot=Join-Path $root $Name;New-Item -ItemType Directory -Force $activeRoot|Out-Null
 $cfg=Get-Content "$ProjectRoot/config/example.json" -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot="$activeRoot/data";$cfg.characterRoot="$ProjectRoot/character";$cfg.uiRoot="$ProjectRoot/ui";$cfg.pluginRoot="$BuildRoot/plugins";$cfg.plugins=@{};$cfg.frontend=@{startupAnimation=$Enabled;reducedMotion=$Reduced};$cfg|ConvertTo-Json -Depth 35|Set-Content "$activeRoot/config.json" -Encoding utf8
 $script:service=Start-Process "$BuildRoot/hibernation-agent.exe" -ArgumentList ('--desktop --config "'+$activeRoot+'/config.json" --ready-file "'+$activeRoot+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$activeRoot/service.log" -RedirectStandardError "$activeRoot/error.log"
}
function Ready{Wait-For {if($service.HasExited){throw 'startup process exited'};Test-Path "$activeRoot/ready.json"} 'ready';$script:origin='http://127.0.0.1:'+((Get-Content "$activeRoot/ready.json" -Raw|ConvertFrom-Json).port)}
function Stop-App{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(8000)-and$service.ExitCode-eq0) 'startup thread exits without orphan windows'}
try{
 $null=[ChatWindowTest]::SetThreadDpiAwarenessContext([IntPtr](-4));Start-App 'animated' $true $false
 Wait-For {$script:splash=[ChatWindowTest]::Find($service.Id,'HibernationStartup');$splash-ne[IntPtr]::Zero-and[ChatWindowTest]::IsWindowVisible($splash)} 'visible startup mark'
 $style=[ChatWindowTest]::GetWindowLongPtrW($splash,-20).ToInt64();Check (($style-band0x080800A8)-eq0x080800A8) 'startup is layered transparent topmost and nonactivating';Check ([ChatWindowTest]::GetForegroundWindow()-ne$splash-and[ChatWindowTest]::Send($splash,0x21)-eq3) 'startup cannot take keyboard activation';Check ([ChatWindowTest]::Send($splash,0x84)-eq-1) 'startup passes pointer input through'
 $geometry=[StartupGeometry]::Measure($splash);Check ([math]::Abs($geometry[0])-le3-and[math]::Abs($geometry[1])-le3) 'startup centers in the monitor work area'
 Ready;Check ((Api '/api/chat').health-eq'ok') 'backend is interactive while decorative animation finishes'
 Wait-For {[ChatWindowTest]::Find($service.Id,'HibernationStartup')-eq[IntPtr]::Zero} 'startup dismisses itself' 4;Check $true 'startup automatically releases its window'
 $main=[ChatWindowTest]::Find($service.Id,'HibernationConversationV2');Check ($main-ne[IntPtr]::Zero) 'main conversation remains after startup'
 $null=[ChatWindowTest]::Send($main,0x10);$null=Api '/api/desktop/show' @{};Check ([StartupGeometry]::Alpha($main)-lt255) 'reopening conversation starts a short fade';Wait-For {[StartupGeometry]::Alpha($main)-eq255} 'conversation fully opaque';Check $true 'fade ends at full opacity'
 $settings=(Api '/api/dashboard').settings;$settings.frontend.reducedMotion=$true;$null=Api '/api/settings' @{settings=$settings};$null=[ChatWindowTest]::Send($main,0x10);$null=Api '/api/desktop/show' @{};Check ([StartupGeometry]::Alpha($main)-eq255) 'reduced motion applies without backend restart';Stop-App
 Start-App 'disabled' $false $true;Ready;Start-Sleep -Milliseconds 250;Check ([ChatWindowTest]::Find($service.Id,'HibernationStartup')-eq[IntPtr]::Zero) 'startup animation can be disabled';Stop-App
 @{completed=$true;checks=$checks;root=$root;timestamp=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content "$root/acceptance.json";Write-Output "Startup desktop passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}}
