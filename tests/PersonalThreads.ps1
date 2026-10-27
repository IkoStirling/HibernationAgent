param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('personal-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='personal-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer personal-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plan($Responses){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Reply{@{version=1;kind='reply';text='留着这个小念头。';calls=@()}}
function Message($Responses){Plan $Responses;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='独立个人关注检查'};Await {!(Api '/api/chat').busy} 'turn';Check (!(Api '/api/chat').call_failed) 'turn completes';$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json;($capture.messages[-1].content|ConvertFrom-Json).tool_results}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'backend exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'backend shutdown'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0],@{id='fixture';label='Personal fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model'})
 $cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.review=@{enabled=$false}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $configPath -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='fixture'}
 $like=@{id='paper-star';expected_revision=0;kind='preference';topic='纸星';note='THREAD_PRIVATE_MARKER 喜欢不太对称的折痕';stance='like';source_ids=@('${event_id}')}
 $intention=@{id='little-story';expected_revision=0;kind='intention';topic='写一个小故事';note='想留一个很短的故事开头';stance='want';source_ids=@('${event_id}')}
 $again=$like.Clone();$again.expected_revision=1
 $r=Message @((Calls @((Tool like self.notice $like),(Tool wish self.notice $intention),(Tool repeat self.notice $again))),(Reply))
 Check ($r[0].ok-and$r[1].ok-and$r[2].result.deduplicated) 'same source cannot repeatedly strengthen a preference'
 Check ($r[0].result.thread.leaning-eq0-and$r[0].result.revision-eq1) 'model cannot jump initial preference strength'
 $r=Message @((Calls @((Tool observe self.observe @{}))),(Reply));Check ($r[0].result.threads.Count-eq2) 'personal interests and intentions are available to later turns'
 $bad=$like.Clone();$bad.id='invented';$bad.source_ids=@('not-real')
 $r=Message @((Calls @((Tool forged self.notice $bad))),(Reply));Check ($r[0].error.code-eq'personal_source_not_available') 'fabricated personal evidence rejected'
 $bad=$again.Clone();$bad.leaning=1
 $r=Message @((Calls @((Tool numeric self.notice $bad))),(Reply));Check ($r[0].error.code-eq'invalid_tool_arguments') 'model cannot directly assign preference numbers'
 $bad=$again.Clone();$bad.topic='改写固定人格'
 $r=Message @((Calls @((Tool identity self.notice $bad))),(Reply));Check ($r[0].error.code-eq'personal_topic_identity') 'existing thread identity cannot be replaced'
 $bad=$again.Clone();$bad.expected_revision=99
 $r=Message @((Calls @((Tool conflict self.notice $bad))),(Reply));Check ($r[0].error.code-eq'personal_revision_conflict') 'stale semantic revision rejected'
 $close=@{id='little-story';expected_revision=1;outcome='completed';note='已经完成';source_ids=@('${event_id}')}
 $r=Message @((Calls @((Tool premature self.close $close))),(Reply));Check ($r[0].error.code-eq'personal_completion_requires_result') 'intent cannot become completed from a user request alone'
 $close.source_ids=@('${last_source_id}')
 $close.source_ids=@('${event_id}')
 $r=Message @((Calls @((Tool merely_observe self.observe @{}))),(Calls @((Tool premature self.close $close))),(Reply));Check ($r[0].error.code-eq'personal_completion_requires_result') 'observing the intention is not evidence that it was completed'
 $close.source_ids=@('${last_source_id}')
 $r=Message @((Calls @((Tool draft workspace.write @{path='little-story.md';content='月亮慢了半拍。'}))),(Calls @((Tool finish self.close $close))),(Reply))
 Check ($r[0].ok-and$r[0].result.thread.status-eq'completed') 'real written draft can resolve a creative intention'
 $avoid=$again.Clone();$avoid.stance='avoid';$avoid.note='这次反而觉得那道折痕有点扎眼'
 $r=Message @((Calls @((Tool contrary self.notice $avoid))),(Reply));Check ($r[0].result.revision-eq2-and[Math]::Abs($r[0].result.thread.leaning)-lt.1) 'contrary experience changes a target without jumping the preference'
 Stop-Backend;Start-Backend
 $r=Message @((Calls @((Tool restored self.observe @{}))),(Reply))
 Check ($r[0].result.threads.Count-eq1-and$r[0].result.threads[0].id-eq'paper-star'-and$r[0].result.threads[0].revision-eq2) 'active concern persists across restart while completed intention stays closed'
 $r=Message @((Calls @((Tool find_closed self.observe @{query='小故事';status='closed'}))),(Reply))
 Check ($r[0].result.threads.Count-eq1-and$r[0].result.threads[0].status-eq'completed'-and$r[0].result.threads[0].resolution-eq'已经完成') 'closed intentions can be found with their resolution without becoming active'
 $r=Message @((Calls @((Tool search self.observe @{query='折痕';limit=1;status='all'}))),(Reply));Check ($r[0].result.threads.Count-eq1-and$r[0].result.threads[0].id-eq'paper-star') 'personal search uses actual disposition content and respects its bounded limit'
 $r=Message @((Calls @((Tool no_match self.observe @{query='UNRELATED_NO_MATCH_MARKER'}))),(Reply));Check ($r[0].result.matching_count-eq0-and$r[0].result.threads.Count-eq0) 'unrelated personal search does not invent a memory'
 Check (!((Api '/api/chat').messages|ConvertTo-Json -Depth 10).Contains('THREAD_PRIVATE_MARKER')) 'internal concern is absent from visible dialogue'
 Stop-Backend
 Check (!([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite))).Contains('THREAD_PRIVATE_MARKER'))) 'personal concern remains encrypted at rest'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Personal threads passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
