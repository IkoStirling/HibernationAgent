param([string]$BuildRoot='',[string]$OutputRoot='E:/Aliyat/Packages',[switch]$Zip,[switch]$RequirePortable)
$ErrorActionPreference='Stop';$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));. (Join-Path $PSScriptRoot BuildPaths.ps1);$BuildRoot=Resolve-HibernationBuildRoot $BuildRoot $project;$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
. (Join-Path $PSScriptRoot PackageFiles.ps1)
Assert-NoReparsePath $OutputRoot;Assert-NoReparsePath $BuildRoot
$cache=Get-Content -LiteralPath (Join-Path $BuildRoot CMakeCache.txt);if(!($cache|Where-Object {$_-eq'CMAKE_BUILD_TYPE:STRING=Release'})){throw 'Package a verified Release build'}
$hostFile=Join-Path $BuildRoot hibernation-agent.exe;if(!(Test-Path -LiteralPath $hostFile)){throw 'Built executable is missing'}
$tools=Join-Path $BuildRoot tools;& (Join-Path $tools VerifyRuntimeTools.ps1) -ToolsRoot $tools
$version=[regex]::Match((Get-Content -LiteralPath (Join-Path $project CMakeLists.txt) -Raw),'project\(HibernationAgent VERSION ([0-9.]+)').Groups[1].Value
$id='XiaoAi-'+$version+'-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8);$destination=Join-Path $OutputRoot $id;New-Item -ItemType Directory -Path $destination -Force|Out-Null
$plan=[Collections.Generic.List[object]]::new()
function Include-File([string]$Source,[string]$Relative){$file=Get-Item -LiteralPath $Source -Force;if($file.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Cannot package linked files'};$plan.Add(@{source=$file.FullName;path=$Relative;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash})}
function Include-Tree([string]$Source,[string]$Relative,[scriptblock]$Accept){
 Assert-NoReparsePath $Source
 if(!(Test-Path -LiteralPath $Source)){return};foreach($item in Get-ChildItem -LiteralPath $Source -Recurse -Force){if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Cannot package linked directories'};if(!$item.PSIsContainer-and(&$Accept $item)){Include-File $item.FullName (Join-Path $Relative ([IO.Path]::GetRelativePath($Source,$item.FullName)))}}
}
foreach($file in Get-ChildItem -LiteralPath $BuildRoot -File){if($file.Name-in@('hibernation-agent.exe','hibernation-installer.exe')-or$file.Extension-eq'.dll'){Include-File $file.FullName ('app/'+$file.Name)}}
Include-Tree (Join-Path $BuildRoot plugins) 'app/plugins' {param($f)($f.Extension-in@('.dll','.exe','.png','.svg','.wav','.txt','.md','.py','.ps1','.js','.mjs')-or$f.Name-in@('LICENSE','NOTICE','CREDITS.md','plugin.json','character.json','source.json','catalog.json','package.json','UPSTREAM.json'))-and$f.Name-notmatch'Tests?\.exe$'-and$f.FullName-notmatch'[\\/](__pycache__|CMakeFiles)[\\/]'}
Include-Tree $tools 'app/tools' {param($f)$f.Extension-notin@('.log','.dpapi','.sqlite')}
Include-Tree (Join-Path $BuildRoot components) 'app/components' {param($f)$f.Extension-in@('.exe','.dll')}
Include-Tree (Join-Path $project out/deps/ffmpeg-sdk/licenses) 'licenses/ffmpeg' {param($f)$true}
foreach($folder in @('ui','character','skills')){Include-Tree (Join-Path $project $folder) $folder {param($f)$f.Extension-in@('.js','.css','.html','.json','.md','.png','.svg')}}
foreach($name in @('Start.ps1','Stop.ps1','Restart.ps1','DeployEngineRelease.ps1','InstallEngineLauncher.ps1','LaunchEngineInstalled.ps1','BuildPaths.ps1','PackageFiles.ps1','InstallPackage.ps1','LaunchInstalled.ps1')){Include-File (Join-Path $PSScriptRoot $name) ('scripts/'+$name)}
foreach($entry in $plan){$target=Assert-ChildPath $destination (Join-Path $destination $entry.path);New-Item -ItemType Directory -Path (Split-Path $target) -Force|Out-Null;Copy-Item -LiteralPath $entry.source -Destination $target;if((Get-FileHash -LiteralPath $target).Hash-ne$entry.sha256-or(Get-FileHash -LiteralPath $entry.source).Hash-ne$entry.sha256){throw 'Source changed while packaging; incomplete directory retained'}}
$cfg=Get-Content -LiteralPath (Join-Path $project config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot='../data';$cfg.characterRoot='../character';$cfg.uiRoot='../ui';$cfg.pluginRoot='../app/plugins';$cfg.draftsRoot='';$cfg.materialsRoot='';$cfg.profiles=@($cfg.profiles|Where-Object {$_.protocol-eq'mock'}|Select-Object -First 1);$cfg.review=@{enabled=$false};$cfg.remoteStorage=@{enabled=$false;kind='none';url=''};$cfg.computer=@{enabled=$false;allowScreenCapture=$false;readRoots=@()};$cfg.memoryWorkshop=@{enabled=$true;backupRoot=''}
foreach($key in @($cfg.plugins.Keys)){$cfg.plugins[$key]=@{enabled=$false}}
if($cfg.skills){$cfg.skills.root='../skills';$cfg.skills.grants=@()}
New-Item -ItemType Directory -Path (Join-Path $destination config) -Force|Out-Null;$cfg|ConvertTo-Json -Depth 75|Set-Content -LiteralPath (Join-Path $destination config/example.json) -Encoding utf8
$start='@echo off'+"`r`n"+'pwsh -NoProfile -File "%~dp0scripts\Start.ps1" -BuildRoot "%~dp0app" -ConfigPath "%~dp0config\local.json"'+"`r`n"
$stop='@echo off'+"`r`n"+'pwsh -NoProfile -File "%~dp0scripts\Stop.ps1" -ConfigPath "%~dp0config\local.json"'+"`r`n"
[IO.File]::WriteAllText((Join-Path $destination Start.cmd),$start,[Text.ASCIIEncoding]::new());[IO.File]::WriteAllText((Join-Path $destination Stop.cmd),$stop,[Text.ASCIIEncoding]::new())
$restart='@echo off'+"`r`n"+'pwsh -NoProfile -File "%~dp0scripts\Restart.ps1" -BuildRoot "%~dp0app" -ConfigPath "%~dp0config\local.json"'+"`r`n"
[IO.File]::WriteAllText((Join-Path $destination Restart.cmd),$restart,[Text.ASCIIEncoding]::new())
$debug=@();foreach($entry in $plan|Where-Object {$_.path-match'\.(exe|dll)$'}){foreach($dependency in Get-PeImports (Join-Path $destination $entry.path)){if($dependency-match'^(ucrtbased|vcruntime\d+(?:_\d+)?d|msvcp\d+(?:_\d+)?d|concrt\d+d)\.dll$'){$debug+=@{file=$entry.path;dependency=$dependency}}}}
$portable=$debug.Count-eq0
$readme=@"
小爱 Windows 本地发行验证包 $version

需要 Windows x64、PowerShell 7，以及依赖检查所列运行库。
Start.cmd 首次创建独立的 config/local.json 与 data/，默认 Mock、关闭插件，不调用真实模型。
关闭对话窗口后仍在托盘；Stop.cmd 保存退出。不要把唯一档案放进版本目录。
这是目录包；可用 scripts/InstallPackage.ps1 安装到独立位置并保留版本回滚。
升级不会迁移或覆盖记忆。跨机恢复请使用密码保护的密钥包与完整存档备份。

是否未发现开发版 CRT 依赖：$portable
详细依赖见 package-manifest.json 的 debug_runtime_dependencies。
存在开发版 CRT 依赖时，本包只代表本机验证产物，不能宣称可在干净电脑独立运行。
没有附带不可再分发的开发版 CRT；视频依赖需后续替换为已验证的 Release SDK。
"@
$readme|Set-Content -LiteralPath (Join-Path $destination README.txt) -Encoding utf8
$inventory=@(Get-ChildItem -LiteralPath $destination -Recurse -File|ForEach-Object {@{path=[IO.Path]::GetRelativePath($destination,$_.FullName).Replace([char]92,[char]47);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;bytes=$_.Length}})
$manifest=@{schema_version=1;name='XiaoAi';version=$version;id=$id;created_utc=[DateTime]::UtcNow.ToString('O');configuration='Release';platform='windows-x64';private_data_included=$false;portable_ready=$portable;debug_runtime_dependencies=$debug;files=$inventory}
$manifest|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $destination package-manifest.json) -Encoding utf8
$null=Test-PackageFiles $destination
if($RequirePortable-and!$portable){throw "Package has development CRT dependencies; diagnostic package retained: $destination"}
if($Zip){Add-Type -AssemblyName System.IO.Compression.FileSystem;[IO.Compression.ZipFile]::CreateFromDirectory($destination,$destination+'.zip',[IO.Compression.CompressionLevel]::Optimal,$false)}
@{package=$destination;files=$inventory.Count;portable_ready=$portable;debug_runtime_dependencies=$debug.Count}|ConvertTo-Json
