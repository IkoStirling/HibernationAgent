param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('failed-attempts-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='failed-attempt-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer failed-attempt-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Call($Id,$Tool,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool=$Tool;args=$Arguments})}}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Plan($Items,$Review=$false){$name=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $root provider/$name) -Encoding utf8}
function Capture{Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 75}
function Results{((Capture).messages[-1].content|ConvertFrom-Json -Depth 65).tool_results}
function Message($Items,$Verdict=$null){Plan $Items;Plan @($(if($Verdict){$Verdict}else{@{decision='approve';reason='ok';guidance=''}})) $true;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='那段故事先停在这里也好。'};Await {!(Api '/api/chat').busy} 'dialogue';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function Start-Backend{$script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'attempt host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean process exit'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.plugins=@{atelier=@{enabled=$true}};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='attempt actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='review';label='attempt review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=0};$configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 $piece=@{id='interrupted-story';expected_revision=0;title='留下半个圆';kind='story';stage='draft';content='圆缺了一截，风在这里停了一会儿。'}
 $null=Message @((Call write atelier.write $piece),(Silent));Check ((Results)[0].ok) 'a real private draft exists'
 $next=$piece.Clone();$next.expected_revision=1;$null=Message @((Call change atelier.write $next),(Silent));Check ((Results)[0].ok) 'the actual work has a later known version'
 $notice=@{id='continue-circle';expected_revision=0;kind='intention';topic='接着那半个圆写一点';note='想看看风会不会回来';stance='want';source_ids=@('${event_id}')}
 $null=Message @((Call wish self.notice $notice),(Silent))
 $append=@{id=$piece.id;expected_revision=1;content='FAILED_PRIVATE_CONTENT_MUST_NOT_BE_WRITTEN';stage='draft'}
 $null=Message @((Call attempt atelier.append $append),(Silent));$failed=(Results)[0]
 Check (!$failed.ok-and$failed.error.code-eq'atelier_revision_conflict'-and$failed.source_id) 'a real failed domain operation returns a provenance id while retaining failure'
 $attemptId=$failed.source_id;$raw=(Api '/api/archive' @{id=$attemptId}).records[0].data
 Check ($raw.kind-eq'tool_attempt'-and$raw.result.attempted-and!$raw.result.succeeded-and!$raw.result.completed-and$raw.result.target.id-eq$piece.id) 'raw encrypted archive distinguishes attempted work from pending or successful work'
 Check (!(($raw|ConvertTo-Json -Depth 20).Contains($append.content))) 'unapplied draft content is not copied into the failed experience'
 $null=Message @((Call read atelier.read @{id=$piece.id}),(Silent));Check ((Results)[0].result.piece.revision-eq2-and(Results)[0].result.piece.content-eq$piece.content) 'failed continuation leaves actual work unchanged'
 $null=Message @((Call see self.notice @{id='interruption-interest';expected_revision=0;kind='interest';topic='句子停住的地方';note='FAILED_ATTEMPT_PRIVATE_MARKER 想下次再看看这一小段停顿';stance='curious';source_ids=@($attemptId)}),(Silent));$thread=(Results)[0].result.thread
 Check ((Results)[0].ok-and$thread.basis.independent_experiences-eq1) 'character can keep its own interpretation of a real failed attempt'
 $null=Message @((Call pretend self.close @{id=$notice.id;expected_revision=1;outcome='completed';note='已经续好了';source_ids=@($attemptId)}),(Silent));Check ((Results)[0].error.code-eq'personal_completion_requires_result') 'failed work cannot complete the larger intention'
 $null=Message @((Call factual memory.remember @{kind='outer';text='我试着继续那段故事，这次没有写入。';source_ids=@($attemptId)}),(Silent));$memory=(Results)[0].result.id;Check ([bool]$memory) 'a remembered failed attempt cites its actual failed source rather than an earlier success'
 $null=Message @((Call again atelier.append $append),(Silent));$second=(Results)[0].source_id
 $null=Message @((Call revisit self.notice @{id='interruption-interest';expected_revision=1;kind='interest';topic='句子停住的地方';note='FAILED_ATTEMPT_PRIVATE_MARKER 还是想下次再看看这一小段停顿';stance='curious';source_ids=@($second)}),(Silent));Check ((Results)[0].result.thread.basis.independent_experiences-eq1) 'repeated failure against the same work does not manufacture independent preference evidence'
 $null=Api '/api/access' @{id=$memory;accessible=$false};$null=Message @((Call private self.observe @{query='句子停住'}),(Silent));Check ((Results)[0].result.threads.Count-eq0) 'restricting the failed origin hides its derived private interest'
 $null=Api '/api/access' @{id=$memory;accessible=$true}
 $null=Message @((Call missing atelier.append @{id='never-existed';expected_revision=1;content='no';stage='draft'}),(Silent));Check (!(Results)[0].source_id) 'invented missing targets do not acquire experience provenance'
 $null=Message @((Call rejected atelier.append $append),(Silent)) @{decision='deny';reason='other';guidance='本测试不批准这次尝试。'}
 $reject=Api '/api/chat';Check ($reject.call_failed) 'semantic refusal prevents invocation'
 $null=Message @((Call history memory.archive_search @{query='实际操作尝试';limit=8}),(Silent));$records=(Results)[0].result.records
 Check (@($records|Where-Object {$_.kind-eq'tool_attempt'}).Count-eq1-and$second-and$second-ne$attemptId) 'repeated same-content attempts stay individually archived and collapse in ordinary retrieval'
 Stop-Backend;Start-Backend;$null=Message @((Call source memory.archive_read @{id=$attemptId}),(Silent));Check ((Results)[0].result.records[0].result.succeeded-eq$false) 'the original failed outcome remains readable after a real restart'
 Stop-Backend;$bytes=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)));Check (!$bytes.Contains('FAILED_ATTEMPT_PRIVATE_MARKER')) 'private meanings remain encrypted on disk'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Failed attempt checks passed: $checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
