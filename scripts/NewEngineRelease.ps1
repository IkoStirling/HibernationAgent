param([Parameter(Mandatory)][string]$PackageRoot,[Parameter(Mandatory)][string]$OutputRoot,[Parameter(Mandatory)][string]$Origin,[string]$BundlePath='/release.bundle',[string]$EngineRoot=(Join-Path $PSScriptRoot '../../AliyatEngine'),[string]$Python='python')
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot PackageFiles.ps1)
$manifest=Test-PackageFiles ([IO.Path]::GetFullPath($PackageRoot))
$producer=Join-Path $EngineRoot AYRuntime/AYApplication/installer/tools/make_install_release.py
& $Python $producer ([IO.Path]::GetFullPath($PackageRoot)) ([IO.Path]::GetFullPath($OutputRoot)) --product XiaoAi --version $manifest.version --entry app/hibernation-agent.exe --origin $Origin --path $BundlePath
if($LASTEXITCODE-ne0){throw '引擎发布包生成失败。'}
