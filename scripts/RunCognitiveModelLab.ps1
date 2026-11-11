param(
 [string]$BuildRoot='',
 [string]$OutputRoot='E:/Aliyat/Backups/HibernationAgent/cognitive-model-experiments',
 [string]$ConfigPath=(Join-Path $PSScriptRoot '../config/local.json'),
 [string]$ActorProfileId='',
 [ValidateRange(4,64)][int]$MaxCalls=24,
 [ValidateRange(20000,500000)][int]$MaxTokens=180000,
 [ValidateSet('baseline','continuity')][string]$Variant='baseline',
 [switch]$Live
)
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Security
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));. (Join-Path $PSScriptRoot BuildPaths.ps1)
$BuildRoot=Resolve-HibernationBuildRoot $BuildRoot $project;$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(!(Test-Path -LiteralPath (Join-Path $BuildRoot 'experiments/cognitive/Hibernation_CognitivePrototype.exe'))){throw '请先构建 CognitivePrototype 实验目标。'}
$volume=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($OutputRoot));if($volume.AvailableFreeSpace-lt32MB){throw '实验盘至少需要 32 MB 可用空间。'}
$run=Join-Path $OutputRoot ('model-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run -Force|Out-Null
$originalPersona=Get-Content -LiteralPath (Join-Path $project character/persona.json) -Raw|ConvertFrom-Json -AsHashtable
$persona=@{name=$originalPersona.displayName};foreach($name in @('identity','attention','voice','boundaries','autonomy','state_response','memory_conduct')){if($originalPersona.character.ContainsKey($name)){$persona[$name]=$originalPersona.character[$name]}}
$connection=@{format='ha.cognitive-model-run.v1';variant=$Variant;live=[bool]$Live;project=$project;buildRoot=$BuildRoot;persona=$persona;maxCalls=$MaxCalls;maxTokens=$MaxTokens;deadline_utc=[DateTime]::UtcNow.AddMinutes(10).ToString('O')}
$restore=@{}
try{
 if($Live){
  $config=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json -AsHashtable
  if(!$config.review.enabled){throw '真实实验需要现有独立审核连接已开启。'}
  $actors=@($config.profiles|Where-Object{$_.enabled-and$_.protocol-ne'mock'-and$_.id-ne$config.review.profileId-and(!$ActorProfileId-or$_.id-eq$ActorProfileId)})
  $reviews=@($config.profiles|Where-Object{$_.enabled-and$_.protocol-ne'mock'-and$_.id-eq$config.review.profileId})
  if($actors.Count-ne1-or$reviews.Count-ne1){throw '请用 ActorProfileId 明确选择一个角色连接，并配置一个独立审核连接。'}
  $dataRoot=if([IO.Path]::IsPathRooted($config.dataRoot)){$config.dataRoot}else{[IO.Path]::GetFullPath((Join-Path (Split-Path ([IO.Path]::GetFullPath($ConfigPath))) $config.dataRoot))}
  $profiles=@();$index=0
  foreach($original in @($actors[0],$reviews[0])){
   $envName=@('HIBERNATION_COG_ACTOR_API_KEY','HIBERNATION_COG_REVIEW_API_KEY')[$index++];$restore[$envName]=[Environment]::GetEnvironmentVariable($envName,'Process');$bytes=$null;$secret=$null
   try{
    $file=Join-Path $dataRoot ('runtime/secrets/'+$original.id+'.dpapi')
    if(Test-Path -LiteralPath $file){$bytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($file),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);$secret=[Text.Encoding]::UTF8.GetString($bytes)}
    else{$secret=[Environment]::GetEnvironmentVariable($original.credentialEnvironment,'Process');if(!$secret){$secret=[Environment]::GetEnvironmentVariable($original.credentialEnvironment,'User')}}
    if(!$secret){throw '连接密钥不可用，未发出 API 请求。'};[Environment]::SetEnvironmentVariable($envName,$secret,'Process')
   }finally{if($bytes){[Array]::Clear($bytes,0,$bytes.Length)};$secret=$null}
   $profile=@{};foreach($field in @('id','origin','basePath','protocol','model','supportsJsonSchema','supportsJsonObject','supportsStreaming','reasoningEffort')){if($original.ContainsKey($field)){$profile[$field]=$original[$field]}}
   $profile.credentialEnvironment=$envName;$profile.maxOutputTokens=[Math]::Min(2048,[int]$original.maxOutputTokens);$profiles+=,$profile
  }
  $connection.profiles=$profiles
 }
 $file=Join-Path $run connection.json;$connection|ConvertTo-Json -Depth 25|Set-Content -LiteralPath $file -Encoding utf8NoBOM
 $node=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe';if(!(Test-Path -LiteralPath $node)){$node=(Get-Command node -ErrorAction Stop).Source}
 & $node (Join-Path $project tools/CognitiveModelLab.mjs) $file
 if($LASTEXITCODE-ne0){throw ('实验未完整通过，已保存诊断与用量：'+$run)}
 Write-Output ('报告：'+(Join-Path $run report.html))
}finally{foreach($name in $restore.Keys){[Environment]::SetEnvironmentVariable($name,$restore[$name],'Process')}}
