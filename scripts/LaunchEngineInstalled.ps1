param([ValidateSet('Start','Stop','Restart','Dashboard')][string]$Action='Start',[switch]$NoBrowser)
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot PackageFiles.ps1)
$settings=Get-Content -LiteralPath (Join-Path $PSScriptRoot launcher.json) -Raw|ConvertFrom-Json
if($Action-eq'Stop'){& (Join-Path $PSScriptRoot Stop.ps1) -ConfigPath $settings.configPath;return}
$pointer=Get-Content -LiteralPath (Join-Path $settings.programRoot current.json) -Raw|ConvertFrom-Json
if($pointer.manifestSha256-notmatch'^[a-fA-F0-9]{64}$'){throw '安装选择的摘要无效。'}
$policy=Join-Path ($settings.programRoot+'.releases') ($pointer.manifestSha256+'/policy.json')
$verified=& (Join-Path $PSScriptRoot hibernation-installer.exe) inspect $policy|ConvertFrom-Json
if(!$verified.ok){throw ('安装版本未通过校验：'+$verified.error)}
$null=Assert-ChildPath $settings.programRoot $verified.executable;$build=Split-Path $verified.executable
if($Action-eq'Restart'){& (Join-Path $PSScriptRoot Restart.ps1) -BuildRoot $build -ConfigPath $settings.configPath -NoBrowser:$NoBrowser}
else{& (Join-Path $PSScriptRoot Start.ps1) -BuildRoot $build -ConfigPath $settings.configPath -Dashboard:($Action-eq'Dashboard') -NoBrowser:$NoBrowser}
