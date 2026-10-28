param(
 [string]$BuildRoot=(Join-Path $PSScriptRoot '../out/pixel-canvas-isolated'),
 [string]$WorkspaceRoot=(Join-Path $PSScriptRoot '../out/pixel-canvas-preview'),
 [string]$AssetRoot='',
 [switch]$Headless,
 [switch]$NoDashboard,
 [switch]$CheckConfig,
 [switch]$Stop
)
$ErrorActionPreference='Stop'
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$WorkspaceRoot=[IO.Path]::GetFullPath($WorkspaceRoot)
if($Stop-and!(Test-Path -LiteralPath $WorkspaceRoot)){Write-Output '预览后台当前未运行。';exit 0}
if(!$AssetRoot){
 $manifest=Join-Path $BuildRoot pixel-canvas-candidate.json
 if(Test-Path -LiteralPath $manifest){$AssetRoot=(Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json).source}
 else{throw '候选资源清单缺失，请通过 AssetRoot 显式指定与该程序匹配的资源目录。'}
}
$AssetRoot=[IO.Path]::GetFullPath($AssetRoot)
if(!(Test-Path -LiteralPath (Join-Path $BuildRoot hibernation-agent.exe))){throw '请先构建像素创作台候选版本，或通过 BuildRoot 指定已验证目录。'}
foreach($relative in @('config/example.json','character/persona.json','ui/pixel-canvas.js','ui/pixel-canvas-examples.js','scripts/Start.ps1','scripts/Stop.ps1')){
 if(!(Test-Path -LiteralPath (Join-Path $AssetRoot $relative))){throw '预览资源不完整，请使用与候选程序匹配的 AssetRoot。'}
}
$marker=Join-Path $WorkspaceRoot '.pixel-canvas-preview'
$purpose='hibernation.pixel-canvas.preview.v1'
if(Test-Path -LiteralPath $WorkspaceRoot){
 if(!(Test-Path -LiteralPath $marker)-or([IO.File]::ReadAllText($marker)).Trim()-ne$purpose){throw '预览目录已经存在且不属于此预览程序。请选择新的 WorkspaceRoot；现有内容不会被覆盖。'}
}else{
 New-Item -ItemType Directory -Path $WorkspaceRoot|Out-Null
 [IO.File]::WriteAllText($marker,$purpose,[Text.UTF8Encoding]::new($false))
}
$configPath=Join-Path $WorkspaceRoot config.json
if(!(Test-Path -LiteralPath $configPath)){
 if($Stop){Write-Output '预览尚未启动。';exit 0}
 $config=Get-Content -LiteralPath (Join-Path $AssetRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $config.dataRoot=Join-Path $WorkspaceRoot data
 $config.draftsRoot=Join-Path $WorkspaceRoot drafts
 $config.materialsRoot=Join-Path $WorkspaceRoot materials
 $config.characterRoot=Join-Path $AssetRoot character
 $config.uiRoot=Join-Path $AssetRoot ui
 $config.pluginRoot=Join-Path $BuildRoot plugins
 $config.profiles=@(@{id='mock';label='独立预览 · 离线演示';protocol='mock';enabled=$true})
 $config.plugins=@{'pixel-canvas'=@{enabled=$true;maximumDocuments=32;defaultScale=3;restoreDesktopArtwork=$true;reducedMotion=$false}}
 $config.review=@{enabled=$false;profileId='';maxRewrites=1}
 $config.computer=@{enabled=$false;allowScreenCapture=$false;readRoots=@()}
 $config.remoteStorage=@{enabled=$false;kind='none';url=''}
 $config.solitudeIntervalMs=604800000;$config.idleBeforeSolitudeMs=86400000
 $config.frontend.startupAnimation=$false
 [IO.File]::WriteAllText($configPath,($config|ConvertTo-Json -Depth 64),[Text.UTF8Encoding]::new($false))
}
# Existing preview settings may have been deliberately configured by the user.
# Preserve their choices, while keeping this launcher on its own archive.
try{
 $existing=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json
 $dataPath=if([IO.Path]::IsPathRooted($existing.dataRoot)){[IO.Path]::GetFullPath($existing.dataRoot)}else{[IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $existing.dataRoot))}
}catch{throw '预览配置无法读取，请修复该预览目录的 config.json；不会改写配置。'}
foreach($binding in @(@{key='uiRoot';expected=(Join-Path $AssetRoot ui)},@{key='characterRoot';expected=(Join-Path $AssetRoot character)},@{key='pluginRoot';expected=(Join-Path $BuildRoot plugins)})){
 $value=$existing.($binding.key)
 if(!($value-is[string])-or[string]::IsNullOrWhiteSpace($value)){throw '预览资源绑定不完整；请选择原版本或新的 WorkspaceRoot，不会改写现有配置。'}
 $resolved=if([IO.Path]::IsPathRooted($value)){[IO.Path]::GetFullPath($value)}else{[IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $value))}
 if(![string]::Equals($resolved,[IO.Path]::GetFullPath($binding.expected),[StringComparison]::OrdinalIgnoreCase)){throw '此预览目录绑定了另一套程序资源；请使用原 BuildRoot/AssetRoot，或选择新的 WorkspaceRoot。'}
}
if(![string]::Equals($dataPath,[IO.Path]::GetFullPath((Join-Path $WorkspaceRoot data)),[StringComparison]::OrdinalIgnoreCase)){throw '独立预览的存档必须位于它自己的 data 目录；不会打开外部或正式档案。'}
if((Test-Path -LiteralPath $dataPath)-and((Get-Item -LiteralPath $dataPath).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw '独立预览不使用链接到其他目录的存档。'}
if(Test-Path -LiteralPath $dataPath){
 $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($dataPath);$entries=0
 while($pending.Count){
  $directory=$pending.Pop()
  foreach($item in Get-ChildItem -LiteralPath $directory -Force){
   if(++$entries-gt10000){throw '独立预览的存档条目过多，请使用新的 WorkspaceRoot。'}
   if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw '独立预览的存档内含目录或文件链接，不会继续打开。'}
   if($item.PSIsContainer){$pending.Push($item.FullName)}
  }
 }
}
foreach($field in @('draftsRoot','materialsRoot')){
 $value=$existing.$field
 if(!($value-is[string])-or[string]::IsNullOrWhiteSpace($value)){throw '独立预览必须明确配置自己的草稿与素材目录。'}
 $resolved=if([IO.Path]::IsPathRooted($value)){[IO.Path]::GetFullPath($value)}else{[IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $value))}
 if(!$resolved.StartsWith($WorkspaceRoot.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw '独立预览的草稿与素材目录必须位于自己的 WorkspaceRoot 中，不会写入外部工作区。'}
 $ancestor=$resolved
 while($ancestor.Length-gt$WorkspaceRoot.Length){
  if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw '独立预览不使用链接到其他目录的工作区。'}
  $ancestor=Split-Path -Parent $ancestor
 }
}
if($Stop){
 $readyPath=Join-Path $dataPath runtime/ready.json
 if(!(Test-Path -LiteralPath $readyPath)){Write-Output '预览后台当前未运行。';exit 0}
 $ready=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json
 $running=Get-Process -Id $ready.pid -ErrorAction SilentlyContinue
 if(!$running){Write-Output '预览后台当前未运行。';exit 0}
 if(![string]::Equals($running.Path,(Join-Path $BuildRoot hibernation-agent.exe),[StringComparison]::OrdinalIgnoreCase)){throw '就绪记录中的进程不属于此预览版本，不会停止其他进程。'}
 & (Join-Path $AssetRoot scripts/Stop.ps1) -ConfigPath $configPath
 exit 0
}
if($CheckConfig){& (Join-Path $BuildRoot hibernation-agent.exe) --config $configPath --check-config;exit $LASTEXITCODE}
Write-Output '启动独立预览：初次使用离线演示与新档案，不导入正式记忆或模型密钥。'
$options=@{BuildRoot=$BuildRoot;ConfigPath=$configPath}
if($Headless){$options.NoBrowser=$true}
if(!$NoDashboard){$options.Dashboard=$true}
& (Join-Path $AssetRoot scripts/Start.ps1) @options
Write-Output '在管理页的“可选插件”打开“像素创作台”。退出请用托盘，或再次运行本脚本加 -Stop。'
