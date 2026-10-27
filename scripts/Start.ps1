param([string]$BuildRoot=(Join-Path $PSScriptRoot '../out/build'),[string]$ConfigPath=(Join-Path $PSScriptRoot '../config/local.json'),[switch]$NoBrowser,[switch]$Dashboard)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Security
if(!(Test-Path -LiteralPath $ConfigPath)){Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../config/example.json') -Destination $ConfigPath}
$ConfigPath=[IO.Path]::GetFullPath($ConfigPath);$config=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json
# A long-lived launcher can predate credentials saved to the Windows user
# environment. Refresh only explicitly configured credential names, and only
# when absent from this process. Never print or persist their values.
$credentialNames=@($config.profiles|ForEach-Object credentialEnvironment)+@($config.plugins.PSObject.Properties|ForEach-Object {$_.Value.credentialEnvironment})
if($config.plugins.napcat.enabled){$credentialNames+=@($(if($config.plugins.napcat.apiTokenEnvironment){$config.plugins.napcat.apiTokenEnvironment}else{'HIBERNATION_NAPCAT_API_TOKEN'}),$(if($config.plugins.napcat.eventSecretEnvironment){$config.plugins.napcat.eventSecretEnvironment}else{'HIBERNATION_NAPCAT_EVENT_SECRET'}))}
foreach($credentialName in @($credentialNames|Where-Object {$_-and$_-match'^[A-Za-z_][A-Za-z0-9_]*$'}|Select-Object -Unique)){
 if([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($credentialName,'Process'))){
  $credentialValue=[Environment]::GetEnvironmentVariable($credentialName,'User');if([string]::IsNullOrEmpty($credentialValue)){$credentialValue=[Environment]::GetEnvironmentVariable($credentialName,'Machine')}
  if(![string]::IsNullOrEmpty($credentialValue)){[Environment]::SetEnvironmentVariable($credentialName,$credentialValue,'Process')}
 }
}
$dataRoot=if([IO.Path]::IsPathRooted($config.dataRoot)){$config.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path $ConfigPath) $config.dataRoot))}
$runtime=Join-Path $dataRoot 'runtime';New-Item -ItemType Directory -Path $runtime -Force|Out-Null
$readyPath=Join-Path $runtime 'ready.json';$tokenPath=Join-Path $runtime '.ipc-token.dpapi';$executable=Join-Path $BuildRoot 'hibernation-agent.exe';$token=$null;$ready=$null
$launcherLock=[IO.File]::Open((Join-Path $runtime '.launcher.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
$knownProcess=$false
if((Test-Path -LiteralPath $readyPath) -and (Test-Path -LiteralPath $tokenPath)){
 try{$candidate=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;$process=Get-Process -Id $candidate.pid -ErrorAction Stop
  if([IO.Path]::GetFullPath($process.Path) -eq [IO.Path]::GetFullPath($executable)){
   $knownProcess=$true
   $bytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($tokenPath),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
   $token=[Text.Encoding]::UTF8.GetString($bytes);$headers=@{Authorization='Bearer '+$token};$null=Invoke-RestMethod -Uri "http://127.0.0.1:$($candidate.port)/api/state" -Headers $headers -TimeoutSec 3;$ready=$candidate
  }
 }catch{$ready=$null;$token=$null}
}
if($knownProcess -and !$ready){throw '后台仍在运行，但本机连接资料不可用；请先停止该后台，再重新启动。'}
if(!$ready){
 if(!(Test-Path -LiteralPath $executable)){throw '请先运行 scripts/Build.ps1。'}
 $archiveProbe=[IO.File]::Open((Join-Path $runtime 'service.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$archiveProbe.Dispose()
 $random=[byte[]]::new(32);[Security.Cryptography.RandomNumberGenerator]::Fill($random);$token=[Convert]::ToHexString($random)
 $encrypted=[Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($token),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
 [IO.File]::WriteAllBytes($tokenPath,$encrypted);[Environment]::SetEnvironmentVariable($config.credentialEnvironment,$token,'Process')
 $previousWrite=if(Test-Path -LiteralPath $readyPath){(Get-Item -LiteralPath $readyPath).LastWriteTimeUtc}else{[DateTime]::MinValue}
 $argumentLine='--config "'+$ConfigPath+'" --ready-file "'+$readyPath+'"'
 if(!$NoBrowser){$argumentLine+=' --desktop'}
 $process=Start-Process -FilePath $executable -ArgumentList $argumentLine -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runtime 'service.log') -RedirectStandardError (Join-Path $runtime 'error.log')
 $deadline=[DateTime]::UtcNow.AddSeconds(20)
 while([DateTime]::UtcNow -lt $deadline){$process.Refresh();if($process.HasExited){throw '后台启动失败，请查看 runtime/error.log（日志不包含密钥）。'}
  if((Test-Path -LiteralPath $readyPath) -and (Get-Item -LiteralPath $readyPath).LastWriteTimeUtc -gt $previousWrite){try{$candidate=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;if($candidate.pid -eq $process.Id){$ready=$candidate;break}}catch{}}
  Start-Sleep -Milliseconds 100
 }
 if(!$ready){throw '后台就绪等待超时。'}
}
if(!$NoBrowser){$null=Invoke-RestMethod -Uri "http://127.0.0.1:$($ready.port)/api/desktop/show" -Method Post -Body '{}' -ContentType 'application/json' -Headers @{Authorization='Bearer '+$token} -TimeoutSec 5}
if($Dashboard){Start-Process -FilePath ("http://127.0.0.1:"+$ready.port+"/#session="+$token)|Out-Null}
Write-Output '后台已运行。关闭对话窗口后会留在托盘；右键托盘可打开管理页面。'
Write-Output '结束后台请从托盘保存退出，或运行 scripts/Stop.ps1。模型密钥可在管理页面设置。'
}finally{$launcherLock.Dispose()}
