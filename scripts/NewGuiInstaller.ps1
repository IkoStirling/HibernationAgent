param([string]$BuildRoot='',[string]$PackageRoot='',[string]$OutputRoot='E:/Aliyat/Installers',[string]$Python='python',[string]$EngineRoot=(Join-Path $PSScriptRoot '../../AliyatEngine'))
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot BuildPaths.ps1);. (Join-Path $PSScriptRoot PackageFiles.ps1)
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$BuildRoot=Resolve-HibernationBuildRoot $BuildRoot $project
if(!$PackageRoot){$pointer=Get-Content (Join-Path $project out/cognitive-runtime-current.json) -Raw|ConvertFrom-Json;$PackageRoot=$pointer.candidate_package}
$manifest=Test-PackageFiles $PackageRoot;$id='XiaoAi-Installer-'+$manifest.version+'-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss');$out=Join-Path $OutputRoot $id;Assert-NoReparsePath $out;New-Item -ItemType Directory -Path $out -Force|Out-Null
$queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue('XiaoAi-Installer.exe');$queue.Enqueue('hibernation-installer.exe');$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while($queue.Count){$name=$queue.Dequeue();if(!$seen.Add($name)){continue};$source=Assert-ChildPath $BuildRoot (Join-Path $BuildRoot $name);Assert-NoReparsePath $source;Copy-Item -LiteralPath $source -Destination (Join-Path $out $name);foreach($dependency in Get-PeImports $source){if(Test-Path -LiteralPath (Join-Path $BuildRoot $dependency)){$queue.Enqueue($dependency)}}}
New-Item -ItemType Directory -Path (Join-Path $out scripts)|Out-Null
foreach($name in @('GuiActivate.ps1','DeployEngineRelease.ps1','InstallEngineLauncher.ps1','LaunchEngineInstalled.ps1','Start.ps1','Stop.ps1','Restart.ps1','BuildPaths.ps1','PackageFiles.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $out scripts/$name)}
Copy-Item -LiteralPath (Join-Path $project plugins/napcat/media/asterlan-idle.png) -Destination (Join-Path $out character.png)
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop();$origin='http://127.0.0.1:'+$port
& $Python (Join-Path $EngineRoot AYRuntime/AYApplication/installer/tools/make_install_release.py) $PackageRoot (Join-Path $out release) --product XiaoAi --version $manifest.version --entry app/hibernation-agent.exe --origin $origin --path /release.bundle *> (Join-Path $out publication.log)
if($LASTEXITCODE-ne0){throw '离线安装包生成失败'}
$pin=(Get-FileHash -LiteralPath (Join-Path $out release/release.json)).Hash.ToLowerInvariant()
$settings=@{schema=1;defaultProgramRoot='E:/Aliyat/Applications/XiaoAiTrial/program';offlineFolder='release';powershell=(Join-Path $PSHOME pwsh.exe);source=@{origin=$origin;path='/release.json';manifestSha256=$pin;allowedOrigins=@($origin);allowLoopbackHttp=$true}}
$runtimePointer=Join-Path $project out/cognitive-runtime-current.json
if(Test-Path -LiteralPath $runtimePointer){$current=Get-Content $runtimePointer -Raw|ConvertFrom-Json;if($current.engine_installation){$settings.existing=@{programRoot=$current.engine_installation.program_root;configPath=(Join-Path $project config/local.json);runtimePointer=$runtimePointer}}}
$settings|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $out launcher-settings.json) -Encoding utf8NoBOM
@"
小爱图形安装器 $($manifest.version)

双击 XiaoAi-Installer.exe。默认独立试装，使用离线模拟，不影响当前小爱。
窗口提供安装、更新、修复、校验和运行。选择“更新本机的小爱”才会沿用现有配置。
请保留 release 文件夹，它是本地完整安装源；不需要临时启动脚本或开发服务器。
不要把存档放入程序版本目录。安装完成后点“运行”，会生成独立的 program.launcher 入口。
此包面向当前Windows x64环境验证，仍需要PowerShell 7及相应运行库；未宣称干净机器免环境运行。
"@|Set-Content -LiteralPath (Join-Path $out README.txt) -Encoding utf8NoBOM
@{installer_root=$out;version=$manifest.version;manifest_sha256=$pin;default_mode='isolated_mock';private_data_included=$false}|ConvertTo-Json
