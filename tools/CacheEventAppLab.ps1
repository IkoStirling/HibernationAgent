param([Parameter(Mandatory)][string]$Phase,[string]$BuildRoot=(Join-Path $PSScriptRoot '../out/build'),[string]$LabRoot=(Join-Path $PSScriptRoot '../artifacts/cache-event-20261029'))
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Security
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$LabRoot=[IO.Path]::GetFullPath($LabRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
if(!$LabRoot.StartsWith((Join-Path $project 'artifacts')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Dedicated artifact lab required'}
$source=Get-Content (Join-Path $project config/local.json) -Raw|ConvertFrom-Json -AsHashtable
$actor=@($source.profiles|Where-Object {$_.enabled-and$_.origin-eq'https://api.deepseek.com'-and$_.id-ne$source.review.profileId})[0];$review=@($source.profiles|Where-Object {$_.id-eq$source.review.profileId-and$_.enabled})[0]
if(!$actor-or!$review){throw 'Configured actor and reviewer required'}
$profiles=@();$restore=@{};$keys=@('HIBERNATION_CACHE_ROLE_KEY','HIBERNATION_CACHE_REVIEW_KEY');$index=0
try{
 foreach($original in @($actor,$review)){
  $name=$keys[$index++];$restore[$name]=[Environment]::GetEnvironmentVariable($name,'Process');$bytes=$null
  try{$file=Join-Path $source.dataRoot ('runtime/secrets/'+$original.id+'.dpapi');if(Test-Path -LiteralPath $file){$bytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($file),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);$secret=[Text.Encoding]::UTF8.GetString($bytes)}else{$secret=[Environment]::GetEnvironmentVariable($original.credentialEnvironment,'Process');if(!$secret){$secret=[Environment]::GetEnvironmentVariable($original.credentialEnvironment,'User')}};if(!$secret){throw 'Configured credential unavailable'};[Environment]::SetEnvironmentVariable($name,$secret,'Process')
  }finally{if($bytes){[Array]::Clear($bytes,0,$bytes.Length)};$secret=$null}
  $profile=@{};foreach($field in @('id','label','enabled','protocol','origin','basePath','model','supportsVision','supportsJsonSchema','supportsJsonObject','supportsToolCalls','supportsStreaming','reasoningEffort','timeoutMs','maxRequestBytes','maxResponseBytes','maxOutputTokens')){if($original.ContainsKey($field)){$profile[$field]=$original[$field]}};$profile.credentialEnvironment=$name;$profiles+=,$profile
 }
 $connection=Join-Path $LabRoot app-connection.json;@{profiles=$profiles;project=$project}|ConvertTo-Json -Depth 30|Set-Content $connection -Encoding utf8NoBOM
 & 'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe' (Join-Path $PSScriptRoot $(if($Phase.StartsWith('activity-')){'CacheActivityLab.mjs'}else{'CacheEventAppLab.mjs'})) $connection $BuildRoot $Phase
 if($LASTEXITCODE-ne0){throw 'App cache experiment stopped; review sanitized evidence'}
}finally{foreach($name in $restore.Keys){[Environment]::SetEnvironmentVariable($name,$restore[$name],'Process')}}
