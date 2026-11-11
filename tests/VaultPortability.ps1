param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$checks=0;$service=$null
$root=Join-Path $ScratchRoot ('portable-vault-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$destinationParent=Join-Path 'E:/Aliyat/RecoveryTests' ('portable-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $destinationParent -Force|Out-Null
$env:HIBERNATION_SESSION_KEY='portable-vault-synthetic-session';$headers=@{Authorization='Bearer '+$env:HIBERNATION_SESSION_KEY};$ready=Join-Path $root ready.json;$configPath=Join-Path $root config.json;$password='synthetic recovery password 42!'
function Check([bool]$Value,[string]$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,[string]$Name,[int]$Seconds=40){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(& $Probe){return};Start-Sleep -Milliseconds 30};throw "TIMEOUT: $Name"}
function Api($Path,$Data=$null){$options=@{Uri=$script:origin+$Path;Headers=$headers;TimeoutSec=20};if($null-ne$Data){$options.Method='Post';$options.ContentType='application/json; charset=utf-8';$options.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 60 -Compress))};Invoke-RestMethod @options}
function Rejection($Path,$Data,[int]$Expected,[string]$Code){$r=Invoke-WebRequest -Uri ($script:origin+$Path) -Headers $headers -Method Post -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 60 -Compress))) -SkipHttpErrorCheck;Check ($r.StatusCode-eq$Expected-and($r.Content|ConvertFrom-Json).error-eq$Code) "rejection $Code"}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log)
 Await {if($service.HasExited){throw 'portable host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} ready
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(15000)-and$service.ExitCode-eq0) 'clean backend exit'}
function Portable($Operation,$Data){$accepted=Api ('/api/vault-portability/'+$Operation) $Data;Check (![string]::IsNullOrEmpty($accepted.operation_id)) "$Operation accepted asynchronously"|Out-Host;Await {$s=Api '/api/vault-portability/status';$s.operation_id-eq$accepted.operation_id-and$s.state-in@('completed','failed')} $Operation;Api '/api/vault-portability/status'}
try{
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 Copy-Item -LiteralPath (Join-Path $ProjectRoot character) -Destination (Join-Path $root character) -Recurse
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $root character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.profiles=@($cfg.profiles[0]);$cfg.review=@{enabled=$false};$cfg.memoryWorkshop=@{enabled=$true;backupRoot=(Join-Path $root backups)};$cfg|ConvertTo-Json -Depth 60|Set-Content $configPath -Encoding utf8
 Start-Backend
 $unauthorized=Invoke-WebRequest -Uri ($origin+'/api/vault-portability/status') -SkipHttpErrorCheck;Check ($unauthorized.StatusCode-eq401) 'portable status requires owner authentication'
 Rejection '/api/vault-portability/export' @{password=$password} 409 'portable_pause_offline_required'
 $null=Api '/api/message' @{request_id='portable-message';text='PORTABLE_SYNTHETIC_ORIGINAL: remember this small unfinished story.'};Await {!(Api '/api/state').busy} 'synthetic dialogue'
 $before=Api '/api/state';$messageCount=$before.messages.Count;Check ($messageCount-ge2) 'real encrypted dialogue exists before backup'
 $null=Api '/api/control' @{paused=$true;offline=$true}
 Rejection '/api/vault-portability/export' @{password='short'} 400 'portable_password'
 $accepted=Api '/api/vault-portability/export' @{password=$password}
 $concurrent=Invoke-WebRequest -Uri ($origin+'/api/message') -Headers $headers -Method Post -ContentType 'application/json' -Body '{"request_id":"portable-concurrent","text":"must not be stored"}' -SkipHttpErrorCheck
 $concurrentCode=($concurrent.Content|ConvertFrom-Json).error;Check ($concurrent.StatusCode-eq409-and$concurrentCode-in@('portable_busy','offline')) 'in-flight mutation stays blocked (or offline after fast completion)'
 Await {(Api '/api/vault-portability/status').state-in@('completed','failed')} export
 $export=Api '/api/vault-portability/status';Check ($export.state-eq'completed'-and$export.result.stores-ge1-and!$export.result.includes_memory_contents) 'portable export has real archive keys and no memories'
 $bundle=$export.result.bundle;$serialized=$bundle|ConvertTo-Json -Depth 30 -Compress;Check (!$serialized.Contains('PORTABLE_SYNTHETIC_ORIGINAL')-and!$serialized.Contains('HIBERNATION_SESSION_KEY')-and!$serialized.Contains($password)) 'bundle reveals no dialogue password or credentials'
 # The archive copy is created by the application's existing consistent backup workflow.
 $secrets=Join-Path $cfg.dataRoot runtime/secrets;New-Item -ItemType Directory -Path $secrets -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $secrets synthetic.dpapi),'SYNTHETIC_CREDENTIAL_EXCLUDED')
 $job=Api '/api/memory-workshop/jobs' @{type='backup';request_id='portable-backup'}
 Await {$j=(Api '/api/memory-workshop').jobs|Where-Object id -eq $job.id;$j.status-in@('completed','failed')} backup 60
 $backup=(Api '/api/memory-workshop').jobs|Where-Object id -eq $job.id;Check ($backup.status-eq'completed') 'Memory Workshop produced completed backup manifest'
 $backupRoot=$backup.path;$manifest=Get-Content -LiteralPath (Join-Path $backupRoot manifest.json) -Raw|ConvertFrom-Json;Check ($manifest.complete-and!$manifest.api_credentials_included) 'completed backup excludes model credentials'
 # Remove only the synthetic backup's machine-bound blob to exercise portable recovery.
 $copiedKey=[IO.Path]::GetFullPath((Join-Path $backupRoot data/runtime/memory-key.dpapi));$backupAbsolute=[IO.Path]::GetFullPath($backupRoot)+[IO.Path]::DirectorySeparatorChar
 if(!$copiedKey.StartsWith($backupAbsolute,[StringComparison]::OrdinalIgnoreCase)){throw 'fixture key deletion escaped synthetic backup'}
 Remove-Item -LiteralPath $copiedKey
 $archive=Join-Path $backupRoot data/archive/records.sqlite;$originalHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
 $verified=Portable verify @{password=$password;bundle=$bundle;backupRoot=$backupRoot};Check ($verified.state-eq'completed'-and$verified.result.verified-and$verified.result.records-gt0) 'verification authenticates actual rows without old DPAPI file'
 Check ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash-eq$originalHash) 'verification preserves original archive bytes'
 $badTarget=Join-Path $destinationParent wrong-password;$wrong=Portable restore @{password='wrong synthetic password';bundle=$bundle;backupRoot=$backupRoot;targetRoot=$badTarget};Check ($wrong.state-eq'failed'-and$wrong.error-eq'portable_unlock_failed'-and!(Test-Path -LiteralPath $badTarget)) 'wrong password creates no target'
 $tampered=$bundle|ConvertTo-Json -Depth 30|ConvertFrom-Json;$packet=$tampered.ciphertext_hex;$tampered.ciphertext_hex=($packet.Substring(0,$packet.Length-1)+$(if($packet.EndsWith('a')){'b'}else{'a'}));$bad=Portable verify @{password=$password;bundle=$tampered;backupRoot=$backupRoot};Check ($bad.state-eq'failed'-and$bad.error-eq'portable_unlock_failed') 'tampered encrypted bundle is rejected'
 $target=Join-Path $destinationParent recovered-data;$restored=Portable restore @{password=$password;bundle=$bundle;backupRoot=$backupRoot;targetRoot=$target};Check ($restored.state-eq'completed'-and$restored.result.restored-and$restored.result.originals_unchanged-and!$restored.result.automatic_activation) 'restore publishes separate verified data without activating it'
 Check ((Get-FileHash -LiteralPath (Join-Path $target archive/records.sqlite) -Algorithm SHA256).Hash-eq$originalHash) 'recovered encrypted database is byte-identical to backup'
 Check (!(Test-Path -LiteralPath (Join-Path $target runtime/secrets/synthetic.dpapi))-and(Test-Path -LiteralPath (Join-Path $target runtime/memory-key.dpapi))) 'only archive master key is rewrapped without API credentials'
 $conflict=Portable restore @{password=$password;bundle=$bundle;backupRoot=$backupRoot;targetRoot=$target};Check ($conflict.state-eq'failed'-and$conflict.error-eq'portable_target_exists') 'existing restored data is never overwritten'
 Check ((Api '/api/state').data_root-eq$cfg.dataRoot-and(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash-eq$originalHash) 'running data and original backup remain unchanged'
 Stop-Backend
 # An actual new process opens the recovered DPAPI key and reads persisted dialogue.
 $cfg.dataRoot=$target;$cfg|ConvertTo-Json -Depth 60|Set-Content $configPath -Encoding utf8;Start-Backend
 $state=Api '/api/state';Check ($state.messages.Count-eq$messageCount-and($state.messages|ConvertTo-Json -Depth 30).Contains('PORTABLE_SYNTHETIC_ORIGINAL')) 'new process decrypts restored dialogue continuity'
 Check ((Api '/api/vault-portability/status').state-eq'idle') 'portable operation secrets and job state do not persist across restart'
 Stop-Backend
 Check (!([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $target archive/records.sqlite))).Contains('PORTABLE_SYNTHETIC_ORIGINAL'))) 'restored archive remains encrypted on disk'
 @{completed=$true;checks=$checks;root=$root;recovered_data=$target;actual_paid_model_calls=0;physical_second_machine_tested=$false;concurrent_rejection=$concurrentCode}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json)
 Write-Output "Portable vault process checks passed: $checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};$password=$null;$bundle=$null;$serialized=$null}
