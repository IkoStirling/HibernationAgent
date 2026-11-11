param([ValidateSet('Install','Rollback')][string]$Action='Install',[string]$PackageRoot='', [string]$InstallRoot='E:/Aliyat/Applications/XiaoAi')
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot PackageFiles.ps1)
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot).TrimEnd([char]92,[char]47)
if($InstallRoot-eq[IO.Path]::GetPathRoot($InstallRoot).TrimEnd([char]92,[char]47)){throw 'Select a dedicated application folder'}
$cursor=$InstallRoot;while($cursor){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Installation through reparse points is not supported'}};$parent=Split-Path $cursor -Parent;if($parent-eq$cursor){break};$cursor=$parent}
New-Item -ItemType Directory -Path $InstallRoot -Force|Out-Null
$installLock=[IO.File]::Open((Join-Path $InstallRoot .install.lock),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
function Write-Atomic([string]$Path,[byte[]]$Bytes){$null=Assert-ChildPath $InstallRoot $Path;Assert-NoReparsePath $Path;New-Item -ItemType Directory -Path (Split-Path $Path) -Force|Out-Null;$temporary=$Path+'.new-'+[Guid]::NewGuid().ToString('N');[IO.File]::WriteAllBytes($temporary,$Bytes);[IO.File]::Move($temporary,$Path,$true)}
function Json-Bytes($Value){[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 80))}
function Version-Root([string]$Id){if($Id-notmatch'^[A-Za-z0-9][A-Za-z0-9._-]{1,100}$'){throw 'Invalid installed version ID'};$path=Assert-ChildPath (Join-Path $InstallRoot versions) (Join-Path (Join-Path $InstallRoot versions) $Id);Assert-NoReparsePath $path;$path}
$pointerFile=Join-Path $InstallRoot current.json;$configFile=Join-Path $InstallRoot config/local.json
try{
 Assert-NoReparsePath (Join-Path $InstallRoot installation-backups)
 Assert-NoReparsePath $pointerFile;Assert-NoReparsePath $configFile
 $oldPointer=if(Test-Path -LiteralPath $pointerFile){[IO.File]::ReadAllBytes($pointerFile)}else{$null};$oldConfig=if(Test-Path -LiteralPath $configFile){[IO.File]::ReadAllBytes($configFile)}else{$null}
 $current=if($null-ne$oldPointer){[Text.Encoding]::UTF8.GetString($oldPointer)|ConvertFrom-Json -AsHashtable}else{$null}
 if($current-and$current.schema_version-ne1){throw 'Unsupported installation pointer'}
 foreach($process in @(Get-Process hibernation-agent -ErrorAction SilentlyContinue)){try{if($process.Path-and$process.Path.StartsWith($InstallRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Exit the installed application before updating or rolling back'}}catch{if($_.Exception.Message-like'Exit the installed*'){throw}}}
 if($Action-eq'Install'){
  if(!$PackageRoot){throw 'PackageRoot is required'};$PackageRoot=[IO.Path]::GetFullPath($PackageRoot);$manifest=Test-PackageFiles $PackageRoot;$id=[string]$manifest.id;$versionRoot=Version-Root $id
  if(Test-Path -LiteralPath $versionRoot){throw 'This version is already installed'}
  New-Item -ItemType Directory -Path $versionRoot -Force|Out-Null
  foreach($entry in @($manifest.files)+@([pscustomobject]@{path='package-manifest.json'})){$from=Assert-ChildPath $PackageRoot (Join-Path $PackageRoot $entry.path);$to=Assert-ChildPath $versionRoot (Join-Path $versionRoot $entry.path);New-Item -ItemType Directory -Path (Split-Path $to) -Force|Out-Null;Copy-Item -LiteralPath $from -Destination $to}
  $null=Test-PackageFiles $versionRoot
 }else{
  if(!$current-or!$current.previous){throw 'No previous version is available'};$id=[string]$current.previous;$versionRoot=Version-Root $id;$manifest=Test-PackageFiles $versionRoot
 }
 $cfg=if($null-ne$oldConfig){[Text.Encoding]::UTF8.GetString($oldConfig)|ConvertFrom-Json -AsHashtable}else{Get-Content -LiteralPath (Join-Path $versionRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable}
 $managed=@{};foreach($key in @('characterRoot','uiRoot','pluginRoot')){
  $suffix=switch($key){'characterRoot'{'character'};'uiRoot'{'ui'};'pluginRoot'{'app/plugins'}}
  $newPath=[IO.Path]::GetRelativePath((Join-Path $InstallRoot config),(Join-Path $versionRoot $suffix)).Replace([char]92,[char]47)
  $priorManaged=if($current-and$current.managed_resources){$current.managed_resources[$key]}else{$null}
  if($null-eq$oldConfig-or($priorManaged-and$cfg[$key]-eq$priorManaged)){$cfg[$key]=$newPath;$managed[$key]=$newPath}
 }
 if($null-eq$oldConfig){$cfg.dataRoot='../data'}
 $priorSkills=if($current-and$current.managed_resources){$current.managed_resources['skills.root']}else{$null}
 if($cfg.skills-and($null-eq$oldConfig-or($priorSkills-and$cfg.skills.root-eq$priorSkills))){$cfg.skills.root=[IO.Path]::GetRelativePath((Join-Path $InstallRoot config),(Join-Path $versionRoot skills)).Replace([char]92,[char]47);$managed['skills.root']=$cfg.skills.root}
 $stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8);$backup=Join-Path $InstallRoot ('installation-backups/'+$stamp);Assert-NoReparsePath $backup;New-Item -ItemType Directory -Path $backup -Force|Out-Null
 if($null-ne$oldConfig){[IO.File]::WriteAllBytes((Join-Path $backup local.json),$oldConfig)};if($null-ne$oldPointer){[IO.File]::WriteAllBytes((Join-Path $backup current.json),$oldPointer)}
 $hostEntry=@($manifest.files|Where-Object path -eq 'app/hibernation-agent.exe');if($hostEntry.Count-ne1){throw 'Package has no unique application executable'}
 $next=@{schema_version=1;current=$id;previous=if($current){$current.current}else{$null};version=$manifest.version;host_sha256=$hostEntry[0].sha256;manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $versionRoot package-manifest.json)).Hash;managed_resources=$managed;updated_utc=[DateTime]::UtcNow.ToString('O');portable_ready=$manifest.portable_ready}
 $oldLaunchers=@{};foreach($helper in @('LaunchInstalled.ps1','PackageFiles.ps1','Start.cmd','Stop.cmd','Restart.cmd','Dashboard.cmd')){$path=Join-Path $InstallRoot $helper;Assert-NoReparsePath $path;$oldLaunchers[$helper]=if(Test-Path -LiteralPath $path){[IO.File]::ReadAllBytes($path)}else{$null}}
 try{
  Write-Atomic $configFile (Json-Bytes $cfg)
  $hostFile=Join-Path $versionRoot app/hibernation-agent.exe;$probe=Start-Process -FilePath $hostFile -ArgumentList ('--config "'+$configFile+'" --check-config') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $backup configuration.log) -RedirectStandardError (Join-Path $backup configuration-error.log)
  if($probe.ExitCode-ne0){throw 'New executable rejected the preserved configuration'}
  foreach($helper in @('LaunchInstalled.ps1','PackageFiles.ps1')){Write-Atomic (Join-Path $InstallRoot $helper) ([IO.File]::ReadAllBytes((Join-Path $versionRoot ('scripts/'+$helper))))}
  foreach($choice in @('Start','Stop','Restart','Dashboard')){$body='@echo off'+"`r`n"+'pwsh -NoProfile -File "%~dp0LaunchInstalled.ps1" -Action '+$choice+"`r`n";Write-Atomic (Join-Path $InstallRoot ($choice+'.cmd')) ([Text.ASCIIEncoding]::new().GetBytes($body))}
  Write-Atomic $pointerFile (Json-Bytes $next)
 }catch{
  foreach($helper in $oldLaunchers.Keys){$path=Join-Path $InstallRoot $helper;if($null-ne$oldLaunchers[$helper]){if(!(Test-Path -LiteralPath $path)-or[Convert]::ToBase64String([IO.File]::ReadAllBytes($path))-ne[Convert]::ToBase64String($oldLaunchers[$helper])){Write-Atomic $path $oldLaunchers[$helper]}}elseif(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}}
  if($null-ne$oldConfig){Write-Atomic $configFile $oldConfig}elseif(Test-Path -LiteralPath $configFile){Remove-Item -LiteralPath $configFile}
  if($null-ne$oldPointer){if(!(Test-Path -LiteralPath $pointerFile)-or[Convert]::ToBase64String([IO.File]::ReadAllBytes($pointerFile))-ne[Convert]::ToBase64String($oldPointer)){Write-Atomic $pointerFile $oldPointer}}elseif(Test-Path -LiteralPath $pointerFile){Remove-Item -LiteralPath $pointerFile}
  throw
 }
 @{completed=$true;action=$Action;install_root=$InstallRoot;current=$id;previous=$next.previous;configuration_preserved=($null-ne$oldConfig);data_touched=$false;started=$false;portable_ready=$manifest.portable_ready;installation_backup=$backup}|ConvertTo-Json
}finally{$installLock.Dispose()}
