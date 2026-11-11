param([Parameter(Mandatory)][string]$SessionRoot,[string]$ConfigPath=(Join-Path $PSScriptRoot ../config/local.json),[string]$DeadlineUtc='2026-10-29T23:23:32Z')
$ErrorActionPreference='Stop';$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ..));$SessionRoot=[IO.Path]::GetFullPath($SessionRoot)
if(!$SessionRoot.StartsWith((Join-Path $project artifacts)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Session must stay inside project artifacts'}
New-Item -ItemType Directory -Force $SessionRoot|Out-Null
if(Test-Path -LiteralPath (Join-Path $SessionRoot budget.json.lock)){throw 'A metered session already owns the ledger; inspect before resuming'}
$source=Get-Content -LiteralPath $ConfigPath -Raw|ConvertFrom-Json -AsHashtable -Depth 100
$actor=@($source.profiles|Where-Object {$_.enabled-and$_.protocol-ne'mock'-and$_.id-ne$source.review.profileId-and$_.origin-eq'https://api.deepseek.com'})
$review=@($source.profiles|Where-Object {$_.enabled-and$_.id-eq$source.review.profileId})
if($actor.Count-ne1-or$review.Count-ne1){throw 'Exactly one configured actor and reviewer required'}
$routes=@{};foreach($batch in @('baseline','updated','probe','contrast')){foreach($role in @('actor','review')){$profile=if($role-eq'actor'){$actor[0]}else{$review[0]};$routes[$batch+'-'+$role]=@{origin=$profile.origin;basePath=$(if($profile.basePath){$profile.basePath}else{'/v1'});model=$profile.model;role=$role;batch=$batch}}}
$cfg=@{version=1;sessionId='continuity-8h-20261029-232332';maxCalls=1000;maxTokens=16000000;deadline=$DeadlineUtc;routes=$routes}
$file=Join-Path $SessionRoot relay-config.json;$cfg|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $file -Encoding utf8NoBOM
$node='C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$process=Start-Process -FilePath $node -ArgumentList ('"'+(Join-Path $project tools/ContinuitySession.mjs)+'" "'+$file+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $SessionRoot relay.log) -RedirectStandardError (Join-Path $SessionRoot relay-error.log)
$until=[DateTime]::UtcNow.AddSeconds(15);do{if($process.HasExited){throw 'Session relay failed'};if(Test-Path -LiteralPath (Join-Path $SessionRoot relay-ready.json)){$ready=Get-Content (Join-Path $SessionRoot relay-ready.json) -Raw|ConvertFrom-Json;if($ready.pid-eq$process.Id){break}};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$until)
if(!$ready-or$ready.pid-ne$process.Id){throw 'Session relay readiness timed out'}
[pscustomobject]@{pid=$process.Id;session=$SessionRoot;max_calls=1000;deadline=$DeadlineUtc}|ConvertTo-Json
