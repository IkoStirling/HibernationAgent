param([string]$ProjectRoot='D:/Aliyat/HibernationAgent',[string]$BuildRoot='E:/Aliyat/Backups/HibernationAgent/optimization-3h-20261030/build',[string]$ScratchRoot='E:/Aliyat/Validation/StableDashboard-20261105',[int]$Port=0)
$ErrorActionPreference='Stop';$root=Join-Path $ScratchRoot ([Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null;$script:service=$null
if(!$Port){$reserve=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$reserve.Start();$Port=$reserve.LocalEndpoint.Port;$reserve.Stop()}
$c=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$c.dataRoot=Join-Path $root data;$c.characterRoot=Join-Path $ProjectRoot character;$c.uiRoot=Join-Path $ProjectRoot ui;$c.pluginRoot=Join-Path $BuildRoot plugins;$c.port=$Port;$c.plugins=@{};$c.profiles=@($c.profiles[0]);$c.review=@{enabled=$false};$c.credentialEnvironment='HIBERNATION_DASHBOARD_FIXTURE_KEY';$c.solitudeIntervalMs=604800000;$c.idleBeforeSolitudeMs=86400000;$c.frontend.startupAnimation=$false;$config=Join-Path $root config.json;$c|ConvertTo-Json -Depth 70|Set-Content $config -Encoding utf8
$r=Join-Path $c.dataRoot runtime;$ready=Join-Path $r ready.json
function Await([scriptblock]$p){$until=[DateTime]::UtcNow.AddSeconds(25);do{if(&$p){return};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$until);throw 'Dashboard fixture timed out'}
function Api{Invoke-RestMethod ('http://127.0.0.1:'+$Port+'/api/state') -Headers $script:headers -TimeoutSec 5}
function StopBackend{& (Join-Path $ProjectRoot scripts/Stop.ps1) -ConfigPath $config|Out-Null;Await {$script:service.Refresh();$script:service.HasExited}}
try{
 & (Join-Path $ProjectRoot scripts/Start.ps1) -BuildRoot $BuildRoot -ConfigPath $config -NoBrowser|Out-Null
 $info=Get-Content $ready -Raw|ConvertFrom-Json;$script:service=Get-Process -Id $info.pid;if($info.port-ne$Port){throw 'Unexpected dynamic port'}
 Add-Type -AssemblyName System.Security;$plain=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes((Join-Path $r .ipc-token.dpapi)),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);try{$script:headers=@{Authorization='Bearer '+[Text.Encoding]::UTF8.GetString($plain)}}finally{[Array]::Clear($plain,0,$plain.Length)};$secretHash=(Get-FileHash (Join-Path $r .ipc-token.dpapi)).Hash;$null=Api
 $conflict=$c.Clone();$conflict.dataRoot=Join-Path $root conflict;$other=Join-Path $root conflict.json;$conflict|ConvertTo-Json -Depth 70|Set-Content $other -Encoding utf8
 $p=Start-Process (Join-Path $BuildRoot hibernation-agent.exe) -ArgumentList ('--config "'+$other+'" --ready-file "'+(Join-Path $root conflict-ready.json)+'"') -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $root conflict.log) -RedirectStandardError (Join-Path $root conflict-error.log)
 if($p.ExitCode-eq0-or(Test-Path (Join-Path $root conflict-ready.json))){throw 'Port collision silently fell back'}
 StopBackend
 & (Join-Path $ProjectRoot scripts/Start.ps1) -BuildRoot $BuildRoot -ConfigPath $config -NoBrowser|Out-Null;$info=Get-Content $ready -Raw|ConvertFrom-Json;$script:service=Get-Process -Id $info.pid
 if($info.port-ne$Port-or(Get-FileHash (Join-Path $r .ipc-token.dpapi)).Hash-ne$secretHash){throw 'Script restart changed origin or session'};$null=Api;StopBackend
 Remove-Item -LiteralPath $ready -ErrorAction SilentlyContinue
 $script:service=Start-Process (Join-Path $BuildRoot hibernation-agent.exe) -ArgumentList ('--config "'+$config+'" --quick-start') -WindowStyle Hidden -PassThru
 Await {Test-Path $ready};if((Get-Content $ready -Raw|ConvertFrom-Json).port-ne$Port-or(Get-FileHash (Join-Path $r .ipc-token.dpapi)).Hash-ne$secretHash){throw 'Quick-start changed origin or session'};$null=Api;StopBackend
 @{passed=$true;port=$Port;script_restart_old_bearer_works=$true;quick_start_old_bearer_works=$true;port_conflict_no_fallback=$true;paid_model_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json)
 Write-Output ('PASS fixed configured port, original browser credential survives script/quick-start, occupied port rejected. Evidence: '+$root)
}finally{if($script:service-and!$script:service.HasExited){try{StopBackend}catch{Stop-Process -Id $script:service.Id -ErrorAction SilentlyContinue}}}
