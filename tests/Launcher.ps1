param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$root=Join-Path $BuildRoot ('launcher/'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null;$env:TEMP=Join-Path $BuildRoot 'tmp';$env:TMP=$env:TEMP
$config=Get-Content -LiteralPath (Join-Path $ProjectRoot 'config/example.json') -Raw|ConvertFrom-Json;$config.dataRoot=Join-Path $root 'data';$config.characterRoot=Join-Path $ProjectRoot 'character';$config.uiRoot=Join-Path $ProjectRoot 'ui'
$configPath=Join-Path $root 'config.json';$config|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $configPath -Encoding utf8;$active=$false
try{
 & (Join-Path $ProjectRoot 'scripts/Start.ps1') -ConfigPath $configPath -BuildRoot $BuildRoot -NoBrowser;$active=$true
 $first=Get-Content -LiteralPath (Join-Path $config.dataRoot 'runtime/ready.json') -Raw|ConvertFrom-Json
 & (Join-Path $ProjectRoot 'scripts/Start.ps1') -ConfigPath $configPath -BuildRoot $BuildRoot -NoBrowser
 $second=Get-Content -LiteralPath (Join-Path $config.dataRoot 'runtime/ready.json') -Raw|ConvertFrom-Json
 if($first.pid-ne$second.pid){throw 'Reconnect launched another backend'}
 & (Join-Path $ProjectRoot 'scripts/Stop.ps1') -ConfigPath $configPath;$active=$false
 if(Get-Process -Id $first.pid -ErrorAction SilentlyContinue){throw 'Backend remained after stop'}
 & (Join-Path $ProjectRoot 'scripts/Start.ps1') -ConfigPath $configPath -BuildRoot $BuildRoot -NoBrowser;$active=$true
 $third=Get-Content -LiteralPath (Join-Path $config.dataRoot 'runtime/ready.json') -Raw|ConvertFrom-Json
 if($third.pid-eq$first.pid){throw 'Expected fresh backend after restart'}
 & (Join-Path $ProjectRoot 'scripts/Stop.ps1') -ConfigPath $configPath;$active=$false
 @{completed=$true;checks=4;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'acceptance.json') -Encoding utf8
 Write-Output 'PASS launcher start, authenticated reconnect, graceful stop and restart'
}finally{if($active){try{& (Join-Path $ProjectRoot 'scripts/Stop.ps1') -ConfigPath $configPath}catch{}}}
