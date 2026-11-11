param([string]$BuildRoot='',[string]$ConfigPath=(Join-Path $PSScriptRoot '../config/local.json'),[switch]$NoBrowser,[switch]$Rebuild,[ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration='Release',[string]$NextPackageRoot='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot BuildPaths.ps1)
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$BuildRoot=Resolve-HibernationRuntimeRoot $BuildRoot $project
$ConfigPath=[IO.Path]::GetFullPath($ConfigPath);$cfg=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json -AsHashtable
$originalConfig=[IO.File]::ReadAllBytes($ConfigPath)
if($NextPackageRoot){
 if($Rebuild){throw '版本切换与原地重建不能同时执行。'}
 . (Join-Path $PSScriptRoot PackageFiles.ps1)
 $NextPackageRoot=[IO.Path]::GetFullPath($NextPackageRoot);$nextManifest=Test-PackageFiles $NextPackageRoot -AllowEngineManifest
}
if($Rebuild-and!(Test-Path -LiteralPath (Join-Path $PSScriptRoot Build.ps1))){throw '此目录是运行包，没有构建脚本；未停止后台。请在源码仓库构建后更新。'}
if($Rebuild-and!(Test-Path -LiteralPath (Join-Path $BuildRoot CMakeCache.txt))){throw '当前是已安装版本；请在独立构建目录构建，再通过引擎安装脚本部署。未停止后台。'}
$data=if([IO.Path]::IsPathRooted($cfg.dataRoot)){$cfg.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path $ConfigPath) $cfg.dataRoot))}
$runtime=Join-Path $data runtime;New-Item -ItemType Directory -Force -Path $runtime|Out-Null
$lock=[IO.File]::Open((Join-Path $runtime .restart.lock),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
Add-Type -AssemblyName System.Security
$report=@{version=1;completed=$false;started_utc=[DateTime]::UtcNow.ToString('O');napcat_restarted=$false;data_replaced=$false;rebuild=[bool]$Rebuild;phase='preflight'}
function Link {
 $readyPath=Join-Path $runtime ready.json;if(!(Test-Path -LiteralPath $readyPath)){return $null}
 $ready=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;$process=Get-Process -Id $ready.pid -ErrorAction SilentlyContinue
 if(!$process){return $null}
 if([IO.Path]::GetFullPath($process.Path)-ne[IO.Path]::GetFullPath((Join-Path $BuildRoot hibernation-agent.exe))){throw '运行实例与指定构建目录不一致，未执行重启。'}
 $bytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes((Join-Path $runtime .ipc-token.dpapi)),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
 try{@{ready=$ready;origin=('http://127.0.0.1:'+$ready.port);headers=@{Authorization='Bearer '+[Text.Encoding]::UTF8.GetString($bytes)}}}finally{[Array]::Clear($bytes,0,$bytes.Length)}
}
function Query($link,$path){Invoke-RestMethod -Uri ($link.origin+$path) -Headers $link.headers -TimeoutSec 8}
try {
 $before=Link;$hostBefore=$null
 if($before){
  $integration=Query $before '/api/desktop/integration'
  if(!$integration.restart_preserves_napcat){throw '当前后台尚不支持此重启协议；未停止后台或 NapCat。请先完成一次程序升级。'}
  $hostBefore=$integration.process_host;$report.napcat_before=$hostBefore;$report.old_pid=$before.ready.pid
  $report.startup_before=[bool]$integration.startup_enabled
  $end=[DateTime]::UtcNow.AddSeconds(35)
  do{$dashboard=Query $before '/api/dashboard';if(!$dashboard.busy){break};Start-Sleep -Milliseconds 250}while([DateTime]::UtcNow-lt$end)
  if($dashboard.busy){throw '后台仍在处理对话，未执行重启；请等待处理结束。'}
  & (Join-Path $PSScriptRoot Stop.ps1) -ConfigPath $ConfigPath -PreserveNapcat
 }
 $report.phase='backend_stopped'
 if($Rebuild){
  # The NapCat host runs from a content-addressed runtime copy, so rebuilding
  # the application/helper in this directory cannot replace its running files.
  & (Join-Path $PSScriptRoot Build.ps1) -BuildRoot $BuildRoot -Action Build -Configuration $Configuration -Targets @('hibernation-agent')
  if($LASTEXITCODE-ne0){throw '构建失败；小爱保持停止，NapCat 保留运行。修复后再次执行此脚本。'}
 }
 if($NextPackageRoot){
  $backup=Join-Path $runtime ('config-before-engine-deploy-'+[Guid]::NewGuid().ToString('N')+'.json');[IO.File]::WriteAllBytes($backup,$originalConfig)
  $cfg.uiRoot=Join-Path $NextPackageRoot ui;$cfg.pluginRoot=Join-Path $NextPackageRoot app/plugins
  $temporary=$ConfigPath+'.new';$cfg|ConvertTo-Json -Depth 90|Set-Content -LiteralPath $temporary -Encoding utf8NoBOM;[IO.File]::Move($temporary,$ConfigPath,$true)
  $BuildRoot=Join-Path $NextPackageRoot app;$report.config_backup=$backup;$report.new_program_root=$NextPackageRoot
 }
 & (Join-Path $PSScriptRoot Start.ps1) -BuildRoot $BuildRoot -ConfigPath $ConfigPath -NoBrowser:$NoBrowser
 $after=Link;if(!$after){throw '后台未能启动；NapCat 没有被脚本重启。'}
 $dashboard=Query $after '/api/dashboard';if($dashboard.health-ne'ok'){throw '重启后的后台健康检查未通过。'}
 if($NextPackageRoot-and$dashboard.version-ne$nextManifest.version){throw '启动的版本与已校验发布清单不一致。'}
 if($NextPackageRoot-and$report.startup_before){$null=Invoke-RestMethod -Uri ($after.origin+'/api/desktop/integration/action') -Headers $after.headers -Method Post -ContentType 'application/json' -Body '{"action":"startup","enabled":true}' -TimeoutSec 8}
 $integration=Query $after '/api/desktop/integration';$hostAfter=$integration.process_host
 if($hostBefore-and$hostBefore.active){if(!$hostAfter.active-or$hostBefore.host_pid-ne$hostAfter.host_pid-or$hostBefore.host_started_ms-ne$hostAfter.host_started_ms){throw 'NapCat 宿主身份发生变化；未尝试自动重启 NapCat。'}}
 $report.completed=$true;$report.phase='verified';$report.new_pid=$after.ready.pid;$report.napcat_after=$hostAfter;$report.health=$dashboard.health
 Write-Output '小爱已重启；NapCat 未重启。'
} catch {$report.failure=$_.Exception.Message;throw}
finally {$report.finished_utc=[DateTime]::UtcNow.ToString('O');$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runtime restart-last.json) -Encoding utf8NoBOM;$before=$null;$after=$null;$lock.Dispose()}
