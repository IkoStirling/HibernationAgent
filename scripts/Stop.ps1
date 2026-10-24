param([string]$ConfigPath=(Join-Path $PSScriptRoot '../config/local.json'))
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Security
$ConfigPath=[IO.Path]::GetFullPath($ConfigPath);$config=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json
$dataRoot=if([IO.Path]::IsPathRooted($config.dataRoot)){$config.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path $ConfigPath) $config.dataRoot))}
$runtime=Join-Path $dataRoot 'runtime';$ready=Get-Content -LiteralPath (Join-Path $runtime 'ready.json') -Raw|ConvertFrom-Json
$encrypted=[IO.File]::ReadAllBytes((Join-Path $runtime '.ipc-token.dpapi'));$token=[Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect($encrypted,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
$null=Invoke-RestMethod -Uri "http://127.0.0.1:$($ready.port)/api/shutdown" -Method Post -Body '{}' -ContentType 'application/json' -Headers @{Authorization='Bearer '+$token} -TimeoutSec 5
$process=Get-Process -Id $ready.pid -ErrorAction SilentlyContinue;if($process){$process|Wait-Process -Timeout 150}
Write-Output '后台已停止，档案和待续任务已保留。'
