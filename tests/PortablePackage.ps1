param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$checks=0;$running=$false
$root=Join-Path $BuildRoot ('packaging/'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$env:TEMP=Join-Path $BuildRoot tmp;$env:TMP=$env:TEMP
. (Join-Path $ProjectRoot scripts/PackageFiles.ps1)
function Check($Ok,[string]$Name){if(!$Ok){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Denied([scriptblock]$Action,[string]$Name){$failed=$false;try{$null=&$Action}catch{$failed=$true};Check $failed $Name}
function Api([string]$Path,$Body=$null){$p=@{Uri=$script:origin+$Path;Headers=$script:headers;TimeoutSec=10};if($null-ne$Body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Body|ConvertTo-Json -Depth 25))};Invoke-RestMethod @p}
function Wait-For([scriptblock]$Action){$deadline=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$deadline){if(&$Action){return};Start-Sleep -Milliseconds 60};throw 'Package runtime timed out'}
try{
 $result=& (Join-Path $ProjectRoot scripts/NewPortablePackage.ps1) -BuildRoot $BuildRoot -OutputRoot (Join-Path $root packages)
 $package=($result|Select-Object -Last 1|ConvertFrom-Json).package
 # Move only this newly generated fixture, within the checked scratch directory.
 $null=Assert-ChildPath $root $package;$relocated=Assert-ChildPath $root (Join-Path $root 'relocated package');Move-Item -LiteralPath $package -Destination $relocated;$package=$relocated
 $manifest=Test-PackageFiles $package;$firstId=$manifest.id
 Check (!$manifest.private_data_included-and$manifest.configuration-eq'Release') 'Release manifest excludes private data'
 Check ((Test-Path -LiteralPath (Join-Path $package Restart.cmd))-and(Test-Path -LiteralPath (Join-Path $package scripts/Restart.ps1))-and(Test-Path -LiteralPath (Join-Path $package app/components/napcat-host/hibernation-napcat-host.exe))) 'package includes restart entry and isolated NapCat host runtime'
 Check (@($manifest.files|Where-Object {$_.path-match'(^|/)(data/|local\.json$)|\.dpapi$'-or($_.path-notlike'app/tools/*'-and$_.path-match'Tests?\.exe$')}).Count-eq0) 'no user configuration, archives, DPAPI blobs or application test executables'
 $cfg=Get-Content (Join-Path $package config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 Check ($cfg.profiles.Count-eq1-and$cfg.profiles[0].protocol-eq'mock'-and!$cfg.computer.enabled-and!$cfg.remoteStorage.enabled) 'safe Mock defaults contain no remote connections'
 # The production host is a GUI-subsystem executable. PowerShell's direct
 # invocation need not wait; check the actual child, not a stale LASTEXITCODE.
 $configProbe=Start-Process -FilePath (Join-Path $package app/hibernation-agent.exe) -ArgumentList ('--config "'+(Join-Path $package config/example.json)+'" --check-config') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $root config-probe.log) -RedirectStandardError (Join-Path $root config-probe-error.log)
 Check ($configProbe.ExitCode-eq0) 'relocated package validates without source paths'
 $install=Join-Path $root installed;$installer=Join-Path $ProjectRoot scripts/InstallPackage.ps1
 $null=& $installer -PackageRoot $package -InstallRoot $install
 Check (Test-Path -LiteralPath (Join-Path $install Restart.cmd)) 'installed launcher provides a backend-only Restart entry'
 $configFile=Join-Path $install config/local.json;$config=Get-Content $configFile -Raw|ConvertFrom-Json -AsHashtable;$config.maxStartsPerHour=321;$config|ConvertTo-Json -Depth 75|Set-Content $configFile -Encoding utf8
 New-Item -ItemType Directory -Path (Join-Path $install data) -Force|Out-Null;Set-Content (Join-Path $install data/keep.txt) 'persistent fixture' -Encoding utf8
 $version=Join-Path $install ('versions/'+$firstId)
 & (Join-Path $version scripts/Start.ps1) -BuildRoot (Join-Path $version app) -ConfigPath $configFile -NoBrowser;$running=$true
 $ready=Get-Content (Join-Path $install data/runtime/ready.json) -Raw|ConvertFrom-Json;$script:origin='http://127.0.0.1:'+$ready.port
 Add-Type -AssemblyName System.Security;$token=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes((Join-Path $install data/runtime/.ipc-token.dpapi)),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);$script:headers=@{Authorization='Bearer '+[Text.Encoding]::UTF8.GetString($token)}
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='本地发行测试'};Wait-For {!(Api '/api/chat').busy}
 Check ((Api '/api/chat').messages.Count-ge2) 'installed Mock conversation runs using packaged character and UI resources'
 Check ((Invoke-WebRequest ($script:origin+'/ui/runtime-control.js') -Headers $script:headers).StatusCode-eq200) 'new runtime panel is present in package'
 Check ((Invoke-WebRequest ($script:origin+'/ui/runtime-control.css') -Headers $script:headers).StatusCode-eq200) 'new runtime panel stylesheet is served by actual backend'
 & (Join-Path $install LaunchInstalled.ps1) -Action Stop;$running=$false
 Check (!(Get-Process -Id $ready.pid -ErrorAction SilentlyContinue)) 'installed launcher saves and stops backend'
 $startFile=Join-Path $version scripts/Start.ps1;$startBytes=[IO.File]::ReadAllBytes($startFile);Add-Content $startFile '# integrity fixture'
 Denied {& (Join-Path $install LaunchInstalled.ps1) -Action Stop} 'installed script modification blocks launcher'
 [IO.File]::WriteAllBytes($startFile,$startBytes)
 $dataBefore=@(Get-ChildItem (Join-Path $install data) -File -Recurse|ForEach-Object {@{path=$_.FullName;hash=(Get-FileHash $_.FullName).Hash}})
 $manifest.id=$firstId+'-next';$manifest|ConvertTo-Json -Depth 30|Set-Content (Join-Path $package package-manifest.json) -Encoding utf8
 $null=& $installer -PackageRoot $package -InstallRoot $install
 $pointer=Get-Content (Join-Path $install current.json) -Raw|ConvertFrom-Json
 Check ($pointer.previous-eq$firstId-and$pointer.current-eq$manifest.id) 'upgrade retains previous verified version'
 $upgraded=Get-Content $configFile -Raw|ConvertFrom-Json;Check ($upgraded.maxStartsPerHour-eq321-and$upgraded.uiRoot.Contains($manifest.id)) 'upgrade preserves user settings and updates managed resources'
 $null=& $installer -Action Rollback -InstallRoot $install
 $rolled=Get-Content $configFile -Raw|ConvertFrom-Json;Check ($rolled.maxStartsPerHour-eq321-and$rolled.uiRoot.Contains($firstId)-and!$rolled.uiRoot.Contains('-next')) 'rollback restores previous resources and keeps configuration'
 foreach($file in $dataBefore){Check ((Get-FileHash $file.path).Hash-eq$file.hash) ('archive preserved: '+[IO.Path]::GetRelativePath((Join-Path $install data),$file.path))}
 $pointerBefore=(Get-FileHash (Join-Path $install current.json)).Hash;$configBefore=(Get-FileHash $configFile).Hash
 $launcher=Join-Path $install LaunchInstalled.ps1;Add-Content $launcher '# previous-launcher-fixture';$launcherBefore=(Get-FileHash $launcher).Hash
 $manifest.id=$firstId+'-blocked';$manifest|ConvertTo-Json -Depth 30|Set-Content (Join-Path $package package-manifest.json) -Encoding utf8
 $lock=[IO.File]::Open((Join-Path $install current.json),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{Denied {& $installer -PackageRoot $package -InstallRoot $install} 'pointer activation failure rejects update'}finally{$lock.Dispose()}
 Check ((Get-FileHash $configFile).Hash-eq$configBefore-and(Get-FileHash $launcher).Hash-eq$launcherBefore-and(Get-FileHash (Join-Path $install current.json)).Hash-eq$pointerBefore) 'failed activation restores launchers, configuration and pointer'
 Add-Content (Join-Path $package README.txt) 'tamper fixture'
 Denied {& $installer -PackageRoot $package -InstallRoot $install} 'modified package is rejected before update'
 Check ((Get-FileHash (Join-Path $install current.json)).Hash-eq$pointerBefore-and(Get-FileHash $configFile).Hash-eq$configBefore) 'failed update preserves active pointer and configuration'
 Denied {Assert-ChildPath $root (Join-Path $root '../escape')} 'path escape is rejected'
 $alias=Join-Path $root alias;New-Item -ItemType Directory $alias|Out-Null;Set-Content (Join-Path $alias a.txt) 'alias fixture';Set-Content (Join-Path $alias unlisted.txt) 'unlisted';$hash=(Get-FileHash (Join-Path $alias a.txt)).Hash
 @{schema_version=1;files=@(@{path='a.txt';sha256=$hash},@{path='a.txt.';sha256=$hash})}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $alias package-manifest.json) -Encoding utf8
 Denied {Test-PackageFiles $alias} 'Windows trailing-dot aliases cannot conceal unlisted files'
 $manifest.files[0].path='../escape';$manifest|ConvertTo-Json -Depth 30|Set-Content (Join-Path $package package-manifest.json) -Encoding utf8
 Denied {Test-PackageFiles $package} 'manifest traversal is rejected'
 @{completed=$true;checks=$checks;root=$root;production_data_touched=$false;real_model_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Portable package integration passed: $checks checks. Evidence: $root"
}finally{if($running){try{& (Join-Path $install LaunchInstalled.ps1) -Action Stop}catch{}}}
