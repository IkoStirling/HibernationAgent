param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Security
$root=Join-Path $BuildRoot ('restart-napcat-tests/'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force -Path $root|Out-Null
$install=Join-Path $root napcat;New-Item -ItemType Directory -Force -Path (Join-Path $install config)|Out-Null
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$token='SYNTHETIC_FIXTURE_NOT_A_REAL_CREDENTIAL';$bytes=[Text.Encoding]::UTF8.GetBytes($token+'.napcat');$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
@{token=$token;port=$port;expectedHash=$hash}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $install config/webui.json)
Copy-Item -LiteralPath (Join-Path $BuildRoot Hibernation_DesktopIntegrationFixture.exe) -Destination (Join-Path $install NapCatWinBootMain.exe)
foreach($file in Get-ChildItem -LiteralPath $BuildRoot -Filter '*.dll' -File){Copy-Item -LiteralPath $file.FullName -Destination $install}
foreach($file in @('QQ.exe','NapCatWinBootHook.dll','napcat.mjs','qqnt.json','loadNapCat.js','fixture-online')){[IO.File]::WriteAllText((Join-Path $install $file),'synthetic fixture')}
$cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.plugins=@{};$cfg.profiles=@($cfg.profiles[0]);$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000
$cfg.desktopIntegration=@{napcat=@{directory=$install;autoStart=$true}}
$config=Join-Path $root config.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $config
function Api($path){
 $ready=Get-Content -LiteralPath (Join-Path $cfg.dataRoot runtime/ready.json) -Raw|ConvertFrom-Json
 $secret=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot runtime/.ipc-token.dpapi)),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
 try{Invoke-RestMethod -Uri ('http://127.0.0.1:'+$ready.port+$path) -Headers @{Authorization='Bearer '+[Text.Encoding]::UTF8.GetString($secret)} -TimeoutSec 5}finally{[Array]::Clear($secret,0,$secret.Length)}
}
$running=$false
try{
 & (Join-Path $ProjectRoot scripts/Start.ps1) -BuildRoot $BuildRoot -ConfigPath $config -NoBrowser;$running=$true
 $end=[DateTime]::UtcNow.AddSeconds(25);do{$s=Api '/api/desktop/integration';if($s.phase-eq'online'-and$s.process_host.active){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end)
 if($s.phase-ne'online'-or!$s.process_host.active){throw 'Synthetic NapCat did not come online'}
 $beforePid=Get-Content -LiteralPath (Join-Path $install fixture.pid) -Raw;$beforeHost=$s.process_host
 $key=(Get-FileHash -LiteralPath (Join-Path $cfg.dataRoot runtime/memory-key.dpapi)).Hash
 & (Join-Path $ProjectRoot scripts/Restart.ps1) -BuildRoot $BuildRoot -ConfigPath $config -NoBrowser
 $after=Api '/api/desktop/integration';$last=Get-Content -LiteralPath (Join-Path $cfg.dataRoot runtime/restart-last.json) -Raw|ConvertFrom-Json
 if(!$last.completed-or$last.old_pid-eq$last.new_pid-or$last.napcat_restarted){throw 'Restart receipt failed'}
 if($beforePid-ne(Get-Content -LiteralPath (Join-Path $install fixture.pid) -Raw)-or$beforeHost.host_pid-ne$after.process_host.host_pid-or$beforeHost.host_started_ms-ne$after.process_host.host_started_ms){throw 'NapCat generation changed during backend restart'}
 if($key-ne(Get-FileHash -LiteralPath (Join-Path $cfg.dataRoot runtime/memory-key.dpapi)).Hash){throw 'Memory key changed'}
 if($after.preserve_on_exit){throw 'Preserve flag leaked into replacement backend'}
 $cfg.desktopIntegration.napcat.autoStart=$false;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $config
 & (Join-Path $ProjectRoot scripts/Restart.ps1) -BuildRoot $BuildRoot -ConfigPath $config -NoBrowser -Rebuild -Configuration Release
 $rebuilt=Api '/api/desktop/integration'
 if($rebuilt.process_host.host_pid-ne$beforeHost.host_pid-or$rebuilt.process_host.host_started_ms-ne$beforeHost.host_started_ms-or$beforePid-ne(Get-Content -LiteralPath (Join-Path $install fixture.pid) -Raw)){throw 'Rebuild restart changed NapCat generation'}
 & (Join-Path $ProjectRoot scripts/Stop.ps1) -ConfigPath $config;$running=$false
 $probe=[Net.Sockets.TcpClient]::new();try{$probe.Connect('127.0.0.1',$port);throw 'Owned NapCat survived ordinary stop'}catch [Net.Sockets.SocketException]{}finally{$probe.Dispose()}
 @{passed=$true;backend_pid_changed=$true;napcat_pid_unchanged=$true;host_generation_unchanged=$true;rebuild_restart_with_autostart_disabled=$true;key_unchanged=$true;normal_stop_closes_owned_napcat=$true;real_qq_messages_sent=0;paid_model_calls=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "PASS restart script preserves exact NapCat process and archive key; ordinary stop cleans up. Evidence: $root"
}finally{if($running){try{& (Join-Path $ProjectRoot scripts/Stop.ps1) -ConfigPath $config}catch{}}}
