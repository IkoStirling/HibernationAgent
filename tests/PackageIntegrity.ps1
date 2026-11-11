param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Alias('ScratchRoot')][string]$BuildRoot=(Join-Path $PSScriptRoot '../out/package-integrity'))
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
. (Join-Path $ProjectRoot scripts/PackageFiles.ps1)
$scratch=Join-Path $BuildRoot ('integrity-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $scratch -Force|Out-Null
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$checks.Add($Name);Write-Output "PASS $Name"}
function Denied([scriptblock]$Action,[string]$Name,[string]$Expected=''){
 $failure=$null;try{$null=&$Action}catch{$failure=$_.Exception.Message}
 Check ($null-ne$failure-and(!$Expected-or$failure-match$Expected)) $Name
}
function Write-Manifest([string]$Directory,$Members){
 $files=@(foreach($member in $Members){@{path=$member;sha256=(Get-FileHash -LiteralPath (Join-Path $Directory $member)).Hash}})
 @{schema_version=1;id='fixture-v1';version='0.0.0';files=$files}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $Directory package-manifest.json) -Encoding utf8
}
function Write-RawManifest([string]$Directory,$Members){@{schema_version=1;files=$Members}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $Directory package-manifest.json) -Encoding utf8}

$package=Join-Path $scratch package;New-Item -ItemType Directory -Path (Join-Path $package assets) -Force|Out-Null
Set-Content -LiteralPath (Join-Path $package assets/readme.txt) -Value 'Fixture content'
$hash=(Get-FileHash -LiteralPath (Join-Path $package assets/readme.txt)).Hash
Write-Manifest $package @('assets/readme.txt');$verified=Test-PackageFiles $package
Check ($verified.id-eq'fixture-v1') 'valid synthetic package verifies'
Write-RawManifest $package @(@{path='assets\readme.txt';sha256=$hash});$null=Test-PackageFiles $package
Check $true 'single Windows separator spelling verifies'
foreach($member in @('assets/readme.txt.','assets/readme.txt ','assets//readme.txt','assets/NUL.txt','assets/COM1.txt','assets/../readme.txt','../escape.txt','assets/readme.txt:stream','C:/escape.txt','/escape.txt')){
 Write-RawManifest $package @(@{path=$member;sha256=$hash});Denied {Test-PackageFiles $package} ('reject member '+$member) 'Invalid|escapes'
}
Write-RawManifest $package @(@{path='assets/readme.txt';sha256=$hash},@{path='ASSETS\README.TXT';sha256=$hash})
Denied {Test-PackageFiles $package} 'reject case and separator duplicate aliases' 'duplicate'
Write-RawManifest $package @(@{path='assets/readme.txt';sha256='invalid'})
Denied {Test-PackageFiles $package} 'reject malformed digest' 'Invalid manifest'
Write-Manifest $package @('assets/readme.txt');Set-Content -LiteralPath (Join-Path $package extra.txt) -Value 'Unlisted'
Denied {Test-PackageFiles $package} 'reject ordinary unlisted file' 'unlisted'
# Regression for the demonstrated Windows alias/count bypass: two distinct
# manifest strings cannot stand in for one real file plus an unlisted file.
Write-RawManifest $package @(@{path='assets/readme.txt';sha256=$hash},@{path='assets/readme.txt.';sha256=$hash})
Denied {Test-PackageFiles $package} 'reject alias plus same-count unlisted file' 'Invalid Windows'
Write-Manifest $package @('assets/readme.txt','extra.txt');Add-Content -LiteralPath (Join-Path $package assets/readme.txt) -Value 'Changed'
Denied {Test-PackageFiles $package} 'reject changed listed content' 'integrity'
Denied {Assert-ChildPath $package (Join-Path $package '../escape')} 'reject sibling traversal' 'escapes'
Denied {Assert-ChildPath $package $package} 'reject root as child destination' 'escapes'

# Launcher fixture: these are text sentinels and harmless scripts. The host
# file is hashed but never executed; no build or real service is needed.
$install=Join-Path $scratch 'installed[fixture]';$version=Join-Path $install versions/fixture-v1
foreach($directory in @('app','scripts','config')){New-Item -ItemType Directory -Path (Join-Path $version $directory) -Force|Out-Null}
New-Item -ItemType Directory -Path (Join-Path $install config) -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot scripts/PackageFiles.ps1) -Destination (Join-Path $install PackageFiles.ps1)
Copy-Item -LiteralPath (Join-Path $ProjectRoot scripts/LaunchInstalled.ps1) -Destination (Join-Path $install LaunchInstalled.ps1)
Set-Content -LiteralPath (Join-Path $version app/hibernation-agent.exe) -Value 'NOT AN EXECUTABLE; HASH FIXTURE ONLY'
Set-Content -LiteralPath (Join-Path $install config/local.json) -Value '{}'
$stub='param([string]$BuildRoot,[string]$ConfigPath,[switch]$Dashboard)'+"`n"+'Set-Content -LiteralPath (Join-Path $PSScriptRoot ../launch-marker.txt) -Value fixture'
Set-Content -LiteralPath (Join-Path $version scripts/Start.ps1) -Value $stub
Write-Manifest $version @('app/hibernation-agent.exe','scripts/Start.ps1')
$pointer=@{schema_version=1;current='fixture-v1';host_sha256=(Get-FileHash -LiteralPath (Join-Path $version app/hibernation-agent.exe)).Hash;manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $version package-manifest.json)).Hash}
function Save-Pointer{$pointer|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $install current.json) -Encoding utf8}
Save-Pointer
& (Join-Path $install LaunchInstalled.ps1) -Action Start
Check (Test-Path -LiteralPath (Join-Path $version launch-marker.txt)) 'verified launcher reaches only harmless stub'
# Remove only this known, non-recursive test marker so the fixture package
# remains exact before checking tampering.
Remove-Item -LiteralPath (Join-Path $version launch-marker.txt)
Add-Content -LiteralPath (Join-Path $version scripts/Start.ps1) -Value '# tampered'
Denied {& (Join-Path $install LaunchInstalled.ps1) -Action Start} 'launcher rejects modified script before executing it' 'integrity'
Check (!(Test-Path -LiteralPath (Join-Path $version launch-marker.txt))) 'rejected script did not execute'
Write-Manifest $version @('app/hibernation-agent.exe','scripts/Start.ps1')
Denied {& (Join-Path $install LaunchInstalled.ps1) -Action Start} 'launcher rejects rewritten manifest despite matching file hashes' 'manifest integrity'

# Directory junction creation needs no symbolic-link privilege on Windows.
# Every link and target lives inside this fresh scratch directory.
if($IsWindows){
 $target=Join-Path $scratch target;New-Item -ItemType Directory -Path $target|Out-Null
 $link=Join-Path $scratch linked;New-Item -ItemType Junction -Path $link -Target $target|Out-Null
 Denied {Assert-NoReparsePath $link} 'reject linked root' 'Reparse'
 Denied {Assert-NoReparsePath (Join-Path $link missing/subdir/file.txt)} 'reject missing leaf below linked ancestor' 'Reparse'
 Denied {& (Join-Path $ProjectRoot scripts/NewPortablePackage.ps1) -BuildRoot (Join-Path $scratch nonexistent) -OutputRoot (Join-Path $link output)} 'packager rejects linked output before touching build' 'Reparse'
 Denied {& (Join-Path $ProjectRoot scripts/NewPortablePackage.ps1) -BuildRoot $link -OutputRoot (Join-Path $scratch unused-output)} 'packager rejects linked build root' 'Reparse'
 $backupInstall=Join-Path $scratch backup-install;New-Item -ItemType Directory -Path $backupInstall|Out-Null
 New-Item -ItemType Junction -Path (Join-Path $backupInstall installation-backups) -Target $target|Out-Null
 Denied {& (Join-Path $ProjectRoot scripts/InstallPackage.ps1) -PackageRoot $package -InstallRoot $backupInstall} 'installer rejects linked backup directory before reading package' 'Reparse'
 $linkedPackage=Join-Path $scratch linked-package;New-Item -ItemType Directory -Path $linkedPackage|Out-Null
 Set-Content -LiteralPath (Join-Path $linkedPackage plain.txt) -Value 'Fixture'
 Write-Manifest $linkedPackage @('plain.txt');New-Item -ItemType Junction -Path (Join-Path $linkedPackage unlisted-directory) -Target $target|Out-Null
 Denied {Test-PackageFiles $linkedPackage} 'package rejects unlisted linked directory' 'reparse'
 $pointer.manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $version package-manifest.json)).Hash;Save-Pointer
 # Both move targets are explicitly checked inside this fixture installation.
 $configDirectory=Assert-ChildPath $install (Join-Path $install config);$savedConfig=Assert-ChildPath $install (Join-Path $install fixture-config-preserved)
 Move-Item -LiteralPath $configDirectory -Destination $savedConfig
 New-Item -ItemType Junction -Path $configDirectory -Target $target|Out-Null
 Denied {& (Join-Path $install LaunchInstalled.ps1) -Action Start} 'launcher rejects linked configuration ancestor' 'Reparse'
 Check (@(Get-ChildItem -LiteralPath $target -Force).Count-eq0) 'rejected links never write into destination target'
}
@{completed=$true;checks=$checks;real_executables_started=0;production_data_touched=$false;scratch=$scratch}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $scratch acceptance.json) -Encoding utf8
Write-Output "Package integrity passed: $($checks.Count) checks. Evidence: $scratch"
