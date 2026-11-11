param([Parameter(Mandatory)][string]$ProgramRoot,[Parameter(Mandatory)][string]$InstallerBuildRoot,[Parameter(Mandatory)][string]$ConfigPath)
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot PackageFiles.ps1)
$root=[IO.Path]::GetFullPath($ProgramRoot)+'.launcher';Assert-NoReparsePath $root;New-Item -ItemType Directory -Force -Path $root|Out-Null
# Copy only the trusted bootstrap and its local dependency closure, not an
# unverified executable from the selected version. OS DLLs remain OS-owned.
$queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue('hibernation-installer.exe');$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while($queue.Count){$name=$queue.Dequeue();if(!$seen.Add($name)){continue};$from=Assert-ChildPath $InstallerBuildRoot (Join-Path $InstallerBuildRoot $name);Assert-NoReparsePath $from;$target=Assert-ChildPath $root (Join-Path $root $name);Assert-NoReparsePath $target;Copy-Item -LiteralPath $from -Destination $target -Force;if((Get-FileHash -LiteralPath $from).Hash-ne(Get-FileHash -LiteralPath $target).Hash){throw '启动器副本未通过校验。'};foreach($dependency in Get-PeImports $from){if(Test-Path -LiteralPath (Join-Path $InstallerBuildRoot $dependency)){$queue.Enqueue($dependency)}}}
foreach($name in @('LaunchEngineInstalled.ps1','Start.ps1','Stop.ps1','Restart.ps1','BuildPaths.ps1','PackageFiles.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $root $name) -Force}
@{schema=1;programRoot=[IO.Path]::GetFullPath($ProgramRoot);configPath=[IO.Path]::GetFullPath($ConfigPath)}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root launcher.json) -Encoding utf8NoBOM
foreach($action in @('Start','Stop','Restart','Dashboard')){[IO.File]::WriteAllText((Join-Path $root ($action+'.cmd')),('@echo off'+"`r`n"+'pwsh -NoProfile -File "%~dp0LaunchEngineInstalled.ps1" -Action '+$action+"`r`n"),[Text.ASCIIEncoding]::new())}
Write-Output $root
