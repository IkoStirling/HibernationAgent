param([Parameter(Mandatory)][string]$InstallerRoot,[Parameter(Mandatory)][string]$EvidenceRoot,[switch]$ActivationOnly)
$ErrorActionPreference='Stop'
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
Add-Type -Path (Join-Path $PSScriptRoot NativeChatTest.cs)
Add-Type -AssemblyName System.Drawing
$InstallerRoot=[IO.Path]::GetFullPath($InstallerRoot);$EvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if((Test-Path -LiteralPath $EvidenceRoot)-and!$ActivationOnly){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Force -Path $EvidenceRoot|Out-Null
$settings=Get-Content -LiteralPath (Join-Path $InstallerRoot launcher-settings.json) -Raw|ConvertFrom-Json -AsHashtable
$settings.Remove('existing');$settings.defaultProgramRoot=Join-Path $EvidenceRoot 'trial/program';$settings.noBrowserOnRun=$true
$testSettings=Join-Path $InstallerRoot ('test-settings-'+[guid]::NewGuid().ToString('N')+'.json')
$config=$settings.defaultProgramRoot+'.userdata/config/local.json';$data=$settings.defaultProgramRoot+'.userdata/data';$ready=Join-Path $data runtime/ready.json
$checks=[Collections.Generic.List[string]]::new();$fixture=$null;$window=[IntPtr]::Zero
function Save-Settings {$settings|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $testSettings -Encoding utf8NoBOM}
function Open-Launcher {
 Save-Settings
 $script:fixture=[PrivateDesktop]::new((Join-Path $InstallerRoot XiaoAi-Installer.exe),$testSettings,(Join-Path $EvidenceRoot unused-ready.json),$InstallerRoot,$false,('--settings "'+$testSettings+'"'))
 $deadline=[datetime]::UtcNow.AddSeconds(15)
 do{$script:window=$fixture.Find('XiaoAiInstallerWindow');if($window-ne[IntPtr]::Zero){$fixture.ShowOwnedWindow($window);return};if($fixture.Child.HasExited){throw ('launcher_exited_'+$fixture.Child.ExitCode)};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow-lt$deadline)
 throw 'launcher_window_timeout'
}
function Close-Launcher {
 if($fixture){[ChatWindowTest]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;if(!$fixture.Child.WaitForExit(15000)){throw 'launcher_close_timeout'};$fixture.Dispose();$script:fixture=$null}
}
function Status {[ChatWindowTest]::Text([ChatWindowTest]::GetDlgItem($window,114))}
function Wait-Status([string]$Pattern,[int]$Seconds=180) {
 $deadline=[datetime]::UtcNow.AddSeconds($Seconds)
 do{if($fixture.Child.HasExited){throw ('launcher_exited_'+$fixture.Child.ExitCode)};$s=Status;if($s-match$Pattern-and[ChatWindowTest]::IsWindowEnabled([ChatWindowTest]::GetDlgItem($window,104))){return $s};if($s-match'^未完成：'){throw $s};Start-Sleep -Milliseconds 150}while([datetime]::UtcNow-lt$deadline)
 throw ('status_timeout: '+(Status))
}
function Action([int]$Id){[ChatWindowTest]::PostMessage($window,0x111,[IntPtr]$Id,[IntPtr]::Zero)|Out-Null}
function Api([string]$Path,$Body=$null){
 $r=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json
 $bytes=[IO.File]::ReadAllBytes((Join-Path $data runtime/.ipc-token.dpapi));$token=[Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect($bytes,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
 $arguments=@{Uri=('http://127.0.0.1:'+$r.port+$Path);Headers=@{Authorization='Bearer '+$token};TimeoutSec=10}
 if($null-ne$Body){$arguments.Method='Post';$arguments.ContentType='application/json';$arguments.Body=($Body|ConvertTo-Json -Depth 10)}
 Invoke-RestMethod @arguments
}
function Test-DetachedActivation {
 $c=Get-Content -LiteralPath $config -Raw|ConvertFrom-Json
 if(@($c.profiles).Count-ne1-or$c.profiles[0].protocol-ne'mock'-or[IO.Path]::GetFullPath($c.dataRoot)-ne[IO.Path]::GetFullPath($data)){throw 'detach_requires_own_mock_fixture'}
 $settings.existing=@{programRoot=$settings.defaultProgramRoot;configPath=$config;runtimePointer=$settings.defaultProgramRoot+'.userdata/runtime-pointer.json'}
 $previous=[int](Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid
 if(Get-Process -Id $previous -ErrorAction SilentlyContinue){& (Join-Path $InstallerRoot scripts/Stop.ps1) -ConfigPath $config -PreserveNapcat *> (Join-Path $EvidenceRoot stop-before-detach.log)}
 Open-Launcher
 [ChatWindowTest]::Send([ChatWindowTest]::GetDlgItem($window,110),0x14e,1,0)|Out-Null
 [ChatWindowTest]::Send($window,0x111,(110-bor(1-shl16)),0)|Out-Null
 Action 104
 $deadline=[datetime]::UtcNow.AddSeconds(180)
 do{$s=Status;if($s-match'正在保存并启动'){break};if($s-match'^未完成：'){throw $s};Start-Sleep -Milliseconds 50}while([datetime]::UtcNow-lt$deadline)
 if($s-notmatch'正在保存并启动'){throw 'activation_phase_not_observed'}
 $job=Get-ChildItem -LiteralPath (Join-Path $data runtime/launcher-jobs) -Directory|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1
 Close-Launcher
 $deadline=[datetime]::UtcNow.AddSeconds(120);$success=$false
 do{try{$result=Get-Content -LiteralPath (Join-Path $job.FullName result.json) -Raw -ErrorAction Stop|ConvertFrom-Json;if($result.completed){if(!$result.ok){throw 'relay_reported_failure'};$null=Api '/api/state';$r=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json;if([int]$r.pid-ne$previous-and(Get-Process -Id $r.pid -ErrorAction SilentlyContinue)){$success=$true;break}}}catch{};Start-Sleep -Milliseconds 200}while([datetime]::UtcNow-lt$deadline)
 if(!$success){throw 'detached_activation_failed'}
 $checks.Add('closing_during_activation_preserves_relay_and_backend')
}
function Capture([string]$Name){
 $c=$fixture.CaptureClient($window);$bitmap=[Drawing.Bitmap]::new($c.Width,$c.Height,[Drawing.Imaging.PixelFormat]::Format32bppRgb)
 try{$rect=[Drawing.Rectangle]::new(0,0,$c.Width,$c.Height);$bits=$bitmap.LockBits($rect,[Drawing.Imaging.ImageLockMode]::WriteOnly,[Drawing.Imaging.PixelFormat]::Format32bppRgb);try{[Runtime.InteropServices.Marshal]::Copy($c.Bgra,0,$bits.Scan0,$c.Bgra.Length)}finally{$bitmap.UnlockBits($bits)};$bitmap.Save((Join-Path $EvidenceRoot $Name),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
}
try{
 if($ActivationOnly){Test-DetachedActivation;@{ok=$true;checks=$checks;paid_calls=0;qq_messages=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot report-detach.json) -Encoding utf8NoBOM;return}
 Open-Launcher
 Wait-Status '选择安装位置'|Out-Null;Capture initial.png
 if(![ChatWindowTest]::IsWindowEnabled([ChatWindowTest]::GetDlgItem($window,101))){throw 'install_disabled'}
 [ChatWindowTest]::SetText([ChatWindowTest]::GetDlgItem($window,111),[IO.Path]::GetPathRoot($settings.defaultProgramRoot))
 Action 101;Wait-Status '请选择小爱专用的空目录'|Out-Null;$checks.Add('disk_root_rejected')
 [ChatWindowTest]::SetText([ChatWindowTest]::GetDlgItem($window,111),$settings.defaultProgramRoot)
 Action 101;Wait-Status '^安装完成'|Out-Null;$checks.Add('graphical_install')
 Action 105;Wait-Status '全部通过校验'|Out-Null;$checks.Add('graphical_verify')
 Action 104;Wait-Status '小爱已运行' 240|Out-Null;$checks.Add('graphical_activate')
 Capture installed.png
 $runtime=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json
 $script:backendPid=[int]$runtime.pid
 $cfg=Get-Content -LiteralPath $config -Raw|ConvertFrom-Json
 if(@($cfg.profiles).Count-ne1-or$cfg.profiles[0].protocol-ne'mock'){throw 'fixture_must_remain_mock'}
 $marker='GUI_INSTALLER_SYNTHETIC_'+[guid]::NewGuid().ToString('N')
 $null=Api '/api/message' @{request_id=[guid]::NewGuid().ToString('N');text=$marker}
 $deadline=[datetime]::UtcNow.AddSeconds(20);do{$chat=Api '/api/chat';if(!$chat.busy){break};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow-lt$deadline)
 if($chat.busy-or$chat.call_failed-or!@($chat.messages|Where-Object text -eq $marker).Count){throw 'mock_dialogue_failed'}
 $keyFiles=@(Get-ChildItem -LiteralPath $data -Force -File -Recurse |Where-Object Name -eq 'memory-key.dpapi')
 if(!$keyFiles.Count){throw 'fixture_key_missing'}
 $keyHashes=@{};foreach($file in $keyFiles){$keyHashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
 Close-Launcher
 if(!(Get-Process -Id $backendPid -ErrorAction SilentlyContinue)){throw 'backend_died_when_launcher_closed'};$checks.Add('backend_survives_launcher_close')
 $settings.existing=@{programRoot=$settings.defaultProgramRoot;configPath=$config;runtimePointer=$settings.defaultProgramRoot+'.userdata/runtime-pointer.json'}
 Open-Launcher
 [ChatWindowTest]::Send([ChatWindowTest]::GetDlgItem($window,110),0x14e,1,0)|Out-Null
 [ChatWindowTest]::Send($window,0x111,(110-bor(1-shl16)),0)|Out-Null
 [ChatWindowTest]::Send([ChatWindowTest]::GetDlgItem($window,113),0xf1,1,0)|Out-Null
 Action 102;Wait-Status '^安装完成，小爱已运行' 240|Out-Null
 $next=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json
 if([int]$next.pid-eq$backendPid){throw 'update_did_not_restart_backend'};$script:backendPid=[int]$next.pid
 foreach($file in $keyHashes.Keys){if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash-ne$keyHashes[$file]){throw 'key_changed'}}
 if(!@((Api '/api/chat').messages|Where-Object text -eq $marker).Count){throw 'dialogue_lost_after_update'}
 $checks.Add('update_activates_new_version_preserving_keys');Capture updated.png
 $before=Get-Content -LiteralPath (Join-Path $settings.defaultProgramRoot current.json) -Raw
 Action 103;Action 106;Wait-Status '^已取消'|Out-Null
 if((Get-Content -LiteralPath (Join-Path $settings.defaultProgramRoot current.json) -Raw)-cne$before){throw 'cancel_changed_selection'};$checks.Add('cancel_preserves_selection')
 Close-Launcher
 Test-DetachedActivation
 $settings.source.manifestSha256='0'*64
 Open-Launcher;Start-Sleep -Milliseconds 300
 if((Status)-notmatch'安装源校验不符'-or[ChatWindowTest]::IsWindowEnabled([ChatWindowTest]::GetDlgItem($window,101))){throw 'untrusted_source_allowed'}
 $checks.Add('untrusted_source_blocks_install');Capture invalid-source.png
 Close-Launcher
 @{ok=$true;checks=$checks;test_data=$data;formal_data_accessed=$false;paid_calls=0;qq_messages=0;completed=[datetime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $EvidenceRoot report.json) -Encoding utf8NoBOM
}catch{
 @{ok=$false;checks=$checks;error=$_.Exception.Message;status=$(if($fixture){Status}else{''})}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $EvidenceRoot report.json) -Encoding utf8NoBOM
 throw
}finally{
 if($fixture){Close-Launcher}
 if(Test-Path -LiteralPath $ready){& (Join-Path $InstallerRoot scripts/Stop.ps1) -ConfigPath $config -PreserveNapcat *> (Join-Path $EvidenceRoot stop.log)}
 if(Test-Path -LiteralPath $testSettings){Remove-Item -LiteralPath $testSettings}
}
Get-Content -LiteralPath (Join-Path $EvidenceRoot report.json) -Raw
