param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs);Add-Type -Path (Join-Path $PSScriptRoot NativeChatTest.cs)
Add-Type -AssemblyName System.Security
$root=Join-Path $BuildRoot ('quick-start-tests/'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null;$desktop=$null;$headers=$null;$checks=0
function Check($v,$n){if(!$v){throw "CHECK FAILED: $n"};$script:checks++;Write-Output "PASS $n"}
function Wait([scriptblock]$p){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$p){return};Start-Sleep -Milliseconds 80};throw 'fixture readiness timed out'}
function Api($path,$value=$null){$p=@{Uri=$script:origin+$path;Headers=$script:headers;TimeoutSec=10};if($null-ne$value){$p.Method='Post';$p.ContentType='application/json';$p.Body=($value|ConvertTo-Json -Depth 10 -Compress)};Invoke-RestMethod @p}
try{
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.plugins=@{};$cfg.profiles=@($cfg.profiles[0]);$cfg.frontend=@{startupAnimation=$true};$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000;$config=Join-Path $root config.json;$readyPath=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 40|Set-Content $config -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$readyPath,$ProjectRoot,$true);$service=$desktop.Child;Wait {if($service.HasExited){throw 'quick startup exited'};Test-Path $readyPath};$ready=Get-Content $readyPath -Raw|ConvertFrom-Json;$script:origin='http://127.0.0.1:'+$ready.port
 $tokenPath=Join-Path $root data/runtime/.ipc-token.dpapi;$before=[IO.File]::ReadAllBytes($tokenPath);$bytes=[Security.Cryptography.ProtectedData]::Unprotect($before,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);try{$script:headers=@{Authorization='Bearer '+[Text.Encoding]::UTF8.GetString($bytes)}}finally{[Array]::Clear($bytes,0,$bytes.Length)}
 Check ((Api '/api/state').health-eq'ok') 'direct login startup publishes a usable authenticated backend'
 $main=$desktop.Find('HibernationConversationV2');Check ($main-ne[IntPtr]::Zero-and ![ChatWindowTest]::IsWindowVisible($main)) 'login startup leaves conversation hidden in tray'
 $raw=[IO.File]::ReadAllBytes((Join-Path $BuildRoot hibernation-agent.exe));$pe=[BitConverter]::ToInt32($raw,0x3c);Check ([BitConverter]::ToUInt16($raw,$pe+24+68)-eq2) 'production executable uses Windows GUI subsystem without console'
 $duplicate=Start-Process -FilePath (Join-Path $BuildRoot hibernation-agent.exe) -ArgumentList ('--quick-start --config "'+$config+'"') -WindowStyle Hidden -PassThru;Check ($duplicate.WaitForExit(10000)-and$duplicate.ExitCode-eq0) 'duplicate login startup exits without replacing the running instance'
 Check ([Convert]::ToHexString([IO.File]::ReadAllBytes($tokenPath))-eq[Convert]::ToHexString($before)) 'duplicate launch preserves the existing IPC credential'
 Check ((Api '/api/state').health-eq'ok') 'original backend remains reachable after duplicate startup'
 $null=Api '/api/desktop/show' @{};Wait {[ChatWindowTest]::IsWindowVisible($main)};Check $true 'conversation can be opened after hidden startup'
 $null=Api '/api/desktop/integration/show-login' @{};Wait {$desktop.Find('HibernationNapcatLogin')-ne[IntPtr]::Zero};$login=$desktop.Find('HibernationNapcatLogin');Check ([ChatWindowTest]::IsWindowVisible($login)) 'operator action opens the native QR login window'
 $null=[ChatWindowTest]::PostMessage($login,0x10,[IntPtr]::Zero,[IntPtr]::Zero);Wait {![ChatWindowTest]::IsWindowVisible($login)};Check ((Api '/api/state').health-eq'ok') 'closing QR window only hides it and leaves XiaoAi running'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(12000)-and$service.ExitCode-eq0) 'desktop and QR host shut down cleanly'
 @{passed=$true;checks=$checks;paid_model_calls=0;real_autorun_modified=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output "Evidence: $root"
}finally{if($desktop){if(!$desktop.Child.HasExited){try{$null=Api '/api/shutdown' @{};$null=$desktop.Child.WaitForExit(5000)}catch{};if(!$desktop.Child.HasExited){Stop-Process -Id $desktop.Child.Id}};$desktop.Dispose()};$headers=$null}