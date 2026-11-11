param([Parameter(Mandatory)][string]$Request)
$ErrorActionPreference='Stop';$r=Get-Content -LiteralPath $Request -Raw|ConvertFrom-Json
$result=@{ok=$false;phase='activation';completed=$false};$lock=$null
try{
 $lock=[IO.File]::Open(($r.configPath+'.activation.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $cfg=Get-Content -LiteralPath $r.configPath -Raw|ConvertFrom-Json
 $data=if([IO.Path]::IsPathRooted($cfg.dataRoot)){$cfg.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path $r.configPath) $cfg.dataRoot))}
 $readyPath=Join-Path $data runtime/ready.json;$current=$r.newBuildRoot
 if(Test-Path -LiteralPath $readyPath){$ready=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;$p=Get-Process -Id $ready.pid -ErrorAction SilentlyContinue;if($p){if((Split-Path $p.Path -Leaf)-ne'hibernation-agent.exe'){throw '运行实例身份不匹配。'};$current=Split-Path $p.Path}}
 if($p-and[IO.Path]::GetFullPath($current)-eq[IO.Path]::GetFullPath($r.newBuildRoot)){
  & (Join-Path $PSScriptRoot Start.ps1) -BuildRoot $current -ConfigPath $r.configPath -NoBrowser:([bool]$r.noBrowser) *> $r.logPath
 }else{
  & (Join-Path $PSScriptRoot DeployEngineRelease.ps1) -ReleaseConfig $r.policyPath -InstallerBuildRoot $r.installerBuildRoot -ConfigPath $r.configPath -CurrentBuildRoot $current -RuntimePointer $r.runtimePointer -AlreadyInstalled -NoBrowser:([bool]$r.noBrowser) *> $r.logPath
 }
 if(!$?){throw '保存或切换后台失败。'}
 $result.ok=$true;$result.phase='running';$result.completed=$true
}catch{$result.error=$_.Exception.Message;$result.completed=$true}
finally{if($lock){$lock.Dispose()};$temporary=$r.resultPath+'.new';$result|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $temporary -Encoding utf8NoBOM;[IO.File]::Move($temporary,$r.resultPath,$true)}
