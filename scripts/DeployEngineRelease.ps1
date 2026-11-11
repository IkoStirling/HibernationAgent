param([Parameter(Mandatory)][string]$ReleaseConfig,[string]$InstallerBuildRoot='',[string]$ConfigPath=(Join-Path $PSScriptRoot '../config/local.json'),[string]$CurrentBuildRoot='',[switch]$NoBrowser,[string]$RuntimePointer='',[switch]$AlreadyInstalled)
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot BuildPaths.ps1);. (Join-Path $PSScriptRoot PackageFiles.ps1)
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$CurrentBuildRoot=Resolve-HibernationRuntimeRoot $CurrentBuildRoot $project;$InstallerBuildRoot=Resolve-HibernationBuildRoot $InstallerBuildRoot $project
$ReleaseConfig=[IO.Path]::GetFullPath($ReleaseConfig);$ConfigPath=[IO.Path]::GetFullPath($ConfigPath)
$release=Get-Content -LiteralPath $ReleaseConfig -Raw|ConvertFrom-Json;$cfg=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json
$program=[IO.Path]::GetFullPath($release.programRoot);$data=if([IO.Path]::IsPathRooted($cfg.dataRoot)){$cfg.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path $ConfigPath) $cfg.dataRoot))}
foreach($protected in @($data,(Split-Path $ConfigPath))){$a=$program.TrimEnd('\','/')+'/';$b=[IO.Path]::GetFullPath($protected).TrimEnd('\','/')+'/';if($a.StartsWith($b,[StringComparison]::OrdinalIgnoreCase)-or$b.StartsWith($a,[StringComparison]::OrdinalIgnoreCase)){throw '安装目录必须与记忆、配置目录完全分离。'}}
$tool=Join-Path $InstallerBuildRoot hibernation-installer.exe
if(!$AlreadyInstalled){$result=& $tool install $ReleaseConfig|ConvertFrom-Json;if(!$result.ok){throw ('安装未完成，已提交状态：'+$result.committed+'；'+$result.error)}}
$verified=& $tool inspect $ReleaseConfig|ConvertFrom-Json
if(!$verified.ok-or$verified.product-ne'XiaoAi'){throw '引擎未能验证小爱安装版本；未停止旧后台。'}
$next=Split-Path (Split-Path $verified.executable);if((Split-Path $verified.executable -Leaf)-ne'hibernation-agent.exe'-or(Split-Path (Split-Path $verified.executable) -Leaf)-ne'app'){throw '发布入口不是预期的小爱程序。'}
$null=Assert-ChildPath $program $next;$applicationManifest=Test-PackageFiles $next -AllowEngineManifest
if($applicationManifest.version-ne$verified.version){throw '外层发布版本与小爱程序版本清单不一致；未停止旧后台。'}
$policyRoot=Join-Path ($program+'.releases') $verified.manifestSha256;Assert-NoReparsePath $policyRoot;New-Item -ItemType Directory -Force -Path $policyRoot|Out-Null
$savedPolicy=Join-Path $policyRoot policy.json;Copy-Item -LiteralPath $ReleaseConfig -Destination $savedPolicy -Force
Copy-Item -LiteralPath (Join-Path $next .manifest.json) -Destination (Join-Path $policyRoot release.json) -Force
$auditRoot=Join-Path $data runtime;New-Item -ItemType Directory -Force -Path $auditRoot|Out-Null
if($cfg.plugins.tts.backend-eq'kana'){
 $speechRuntime=Join-Path $data runtime/speech/runtime.json
 if(!(Test-Path -LiteralPath $speechRuntime)){
  $oldPlugins=if($cfg.pluginRoot){$cfg.pluginRoot}else{Join-Path $CurrentBuildRoot plugins}
  $legacy=Join-Path $oldPlugins tts/runtime.json
  if(!(Test-Path -LiteralPath $legacy)){throw '原Kana声库运行配置不可用；未停止旧后台。'}
  $voice=Get-Content -LiteralPath $legacy -Raw|ConvertFrom-Json
  if(!(Test-Path -LiteralPath $voice.pythonExecutable -PathType Leaf)-or!(Test-Path -LiteralPath $voice.modules -PathType Container)){throw '原Kana声库依赖不可用；未停止旧后台。'}
  New-Item -ItemType Directory -Force -Path (Split-Path $speechRuntime)|Out-Null;Copy-Item -LiteralPath $legacy -Destination $speechRuntime
 }
}
$preflight=Start-Process -FilePath $verified.executable -ArgumentList ('--config "'+$ConfigPath+'" --check-config') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $auditRoot install-config-check.log) -RedirectStandardError (Join-Path $auditRoot install-config-error.log)
if($preflight.ExitCode-ne0){throw '候选版本拒绝现有配置；未停止旧后台。'}
$key=Join-Path $data runtime/memory-key.dpapi;$keyHash=if(Test-Path -LiteralPath $key){(Get-FileHash -LiteralPath $key).Hash}else{''}
# Installation never touches the current backend; only the verified selection
# crosses this controlled restart boundary. Keep external data/config in place.
& (Join-Path $PSScriptRoot Restart.ps1) -BuildRoot $CurrentBuildRoot -ConfigPath $ConfigPath -NextPackageRoot $next -NoBrowser:$NoBrowser
if(!$?){throw '版本已安装，但后台切换失败；旧程序和外部存档均保留。'}
if($keyHash-and$keyHash-ne(Get-FileHash -LiteralPath $key).Hash){throw '原密钥指纹变化，停止后续部署记录。'}
$launcher=& (Join-Path $PSScriptRoot InstallEngineLauncher.ps1) -ProgramRoot $program -InstallerBuildRoot $InstallerBuildRoot -ConfigPath $ConfigPath
$pointerFile=if($RuntimePointer){[IO.Path]::GetFullPath($RuntimePointer)}else{Join-Path $project out/cognitive-runtime-current.json}
if(Test-Path -LiteralPath $pointerFile){$pointer=Get-Content -LiteralPath $pointerFile -Raw|ConvertFrom-Json -AsHashtable;$pointer.build_root=Join-Path $next app;$pointer.application_version=$verified.version;$pointer.host_sha256=(Get-FileHash -LiteralPath $verified.executable).Hash;$pointer.engine_installation=@{program_root=$program;release_config=$savedPolicy;installer_executable=(Join-Path $launcher hibernation-installer.exe);launcher_root=$launcher;manifest_sha256=$verified.manifestSha256;version=$verified.version;selected_root=$next};$pointer|ConvertTo-Json -Depth 40|Set-Content -LiteralPath $pointerFile -Encoding utf8NoBOM}
@{completed=$true;version=$verified.version;program_root=$program;selected_root=$next;data_root=$data;original_key_unchanged=$true;napcat_restarted=$false}|ConvertTo-Json
