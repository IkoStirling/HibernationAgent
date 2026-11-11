param([string]$ConfigPath=(Join-Path $PSScriptRoot '../config/local.json'),[switch]$PreserveNapcat)
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Security
$ConfigPath=[IO.Path]::GetFullPath($ConfigPath);$config=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json
$dataRoot=if([IO.Path]::IsPathRooted($config.dataRoot)){$config.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path $ConfigPath) $config.dataRoot))}
$runtime=Join-Path $dataRoot 'runtime';$ready=Get-Content -LiteralPath (Join-Path $runtime 'ready.json') -Raw|ConvertFrom-Json
$encrypted=[IO.File]::ReadAllBytes((Join-Path $runtime '.ipc-token.dpapi'));$token=[Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect($encrypted,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
# Retain the process handle before asking it to exit. Looking it up afterwards
# and piping to Wait-Process races a normal, already-completed shutdown.
$process=Get-Process -Id $ready.pid -ErrorAction SilentlyContinue
if($process){try{$null=$process.Handle}catch{if(!$process.HasExited){throw};$process=$null}}
if($PreserveNapcat){
 $integration=Invoke-RestMethod -Uri "http://127.0.0.1:$($ready.port)/api/desktop/integration" -Headers @{Authorization='Bearer '+$token} -TimeoutSec 5
 if(!$integration.restart_preserves_napcat){throw '此后台尚不支持保留 NapCat 的重启，请先更新程序；未关闭任何进程。'}
 $null=Invoke-RestMethod -Uri "http://127.0.0.1:$($ready.port)/api/desktop/integration/action" -Method Post -Body '{"action":"prepare_restart"}' -ContentType 'application/json' -Headers @{Authorization='Bearer '+$token} -TimeoutSec 5
}
try{$null=Invoke-RestMethod -Uri "http://127.0.0.1:$($ready.port)/api/shutdown" -Method Post -Body '{}' -ContentType 'application/json' -Headers @{Authorization='Bearer '+$token} -TimeoutSec 5}
catch{if($PreserveNapcat){try{$null=Invoke-RestMethod -Uri "http://127.0.0.1:$($ready.port)/api/desktop/integration/action" -Method Post -Body '{"action":"cancel_restart"}' -ContentType 'application/json' -Headers @{Authorization='Bearer '+$token} -TimeoutSec 3}catch{}};throw}
if($process){try{if(!$process.WaitForExit(150000)){throw '后台仍在保存，请等待后再检查；尚未强制结束进程。'}}finally{$process.Dispose()}}
Write-Output '后台已停止，档案和待续任务已保留。'
