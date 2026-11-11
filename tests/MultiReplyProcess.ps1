param(
 [Parameter(Mandatory)][string]$Executable,
 [Parameter(Mandatory)][string]$ProviderExecutable,
 [Parameter(Mandatory)][string]$ProjectRoot,
 [Parameter(Mandatory)][string]$ScratchRoot
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
$root=Join-Path $ScratchRoot ('multi-reply-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force|Out-Null
$candidateHash=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
$service=$null;$provider=$null;$checks=[Collections.Generic.List[string]]::new();$completed=$false;$failure=''
$savedEnvironment=@{TEMP=$env:TEMP;TMP=$env:TMP;HIBERNATION_SESSION_KEY=$env:HIBERNATION_SESSION_KEY}
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='multi-reply-local-fixture'
function Check([bool]$Value,[string]$Name){if(!$Value){throw "CHECK FAILED: $Name"};$checks.Add($Name);Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,[string]$Name,[int]$Seconds=30){$deadline=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$deadline){if(&$Probe){return};Start-Sleep -Milliseconds 35};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$parameters=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer multi-reply-local-fixture'};TimeoutSec=12};if($null-ne$Data){$parameters.Method='Post';$parameters.ContentType='application/json';$parameters.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 70 -Compress))};Invoke-RestMethod @parameters}
function Plan($Value,[bool]$Review=$false,[int]$Delay=0){@{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$Value;delay_ms=$Delay})}|ConvertTo-Json -Depth 70|Set-Content -LiteralPath (Join-Path $providerRoot $(if($Review){'review-plan.json'}else{'plan.json'})) -Encoding utf8}
function Reply($Parts,$Text=$null){@{version=1;kind='reply';text=$Text;messages=@($Parts);calls=@();presentation=@{speech=$false;bubble=$false}}}
function Request([string]$Id,[string]$Text){Api '/api/message' @{request_id=$Id;text=$Text}}
function Idle{Await {!(Api '/api/chat').busy} 'turn completion'}
function RequestRecord([string]$Id){(Request $Id 'duplicate lookup only').request}
function Same($Left,$Right){(ConvertTo-Json -InputObject @($Left) -Compress -Depth 20)-ceq(ConvertTo-Json -InputObject @($Right) -Compress -Depth 20)}
function Start-Host{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root ('service-'+$hostOrdinal+'.log')) -RedirectStandardError (Join-Path $root ('service-'+$hostOrdinal+'-error.log'))
 Await {$service.Refresh();if($service.HasExited){throw 'Synthetic host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'synthetic host readiness'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Host{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'synthetic host saves and exits cleanly'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Path $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'" 600') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'loopback provider readiness'
 $fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $character=Join-Path $root synthetic-character;New-Item -ItemType Directory -Path $character|Out-Null
 @{schemaVersion=1;version=1;id='multi-reply-fixture';displayName='合成角色 M';notice='完全合成的多条协议检查。';identity='只存在于隔离测试中的虚构角色。';attention=@('合成对话');boundaries=@('按整组审核的原话发言，可以沉默。');character=@{dialogue=@('自然交谈。')}}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character persona.json) -Encoding utf8
 @{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='合成初始';description='隔离协议检查。';next='';minimumEvidence=0});reviewPolicy='不改变阶段。'}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character stages.json) -Encoding utf8
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false;readRoots=@()};$cfg.skills=@{enabled=$false;useBuiltins=$false;root=(Join-Path $root skills);grants=@()}
 $cfg.draftsRoot=Join-Path $root workspace/drafts;$cfg.materialsRoot=Join-Path $root workspace/materials;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.contextControl=@{enabled=$false};$cfg.maxStartsPerHour=1000
 $cfg.profiles=@(@{id='mock';label='Mock';enabled=$true;protocol='mock'},@{id='actor';label='Local actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;maxOutputTokens=8192},@{id='review';label='Local reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=0;retainConversationFocus=$false}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 70|Set-Content -LiteralPath $configPath -Encoding utf8
 $hostOrdinal=1;Start-Host;$null=Api '/api/model' @{id='actor'}
 $parts=@('先看这个合成例子。','第二条保留自己的边界。','最后补充一点。');$canonical=$parts-join"`n`n";$bundleId='multi-reply-valid'
 Plan (Reply $parts);Plan @{decision='approve';reason='ok';guidance=''} $true 1400
 $before=@((Api '/api/chat').messages);$sent=Request $bundleId '请用自己选择的消息边界回答。'
 Await {Test-Path -LiteralPath (Join-Path $providerRoot review-last-request.json)} 'whole bundle reaches reviewer'
 Check (@((Api '/api/chat').messages|Where-Object role -eq assistant).Count-eq0) 'no part appears while whole bundle is waiting for review'
 $review=Model-ReviewEvent (Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 90)
 Check ((Same $review.candidate.messages $parts)-and$review.candidate.text-ceq$canonical) 'one reviewer sees all authored parts and their canonical meaning'
 Idle;$chat=@((Api '/api/chat').messages);$published=@($chat|Where-Object role -eq assistant)
 Check ($published.Count-eq3-and(Same $published.text $parts)) 'approved bundle publishes three independent messages in order'
 Check (@($published.reply_bundle_id|Select-Object -Unique).Count-eq1-and$published[0].reply_bundle_id-and(Same $published.message_index @(0,1,2))-and@($published|Where-Object message_count -ne 3).Count-eq0) 'messages retain one bundle identity and exact ordinal metadata'
 $record=RequestRecord $bundleId
 Check ($record.status-eq'completed'-and$record.message_bundle_authored-and(Same $record.reply_ids $published.id)) 'idempotent request API exposes every accepted reply id in order'
 foreach($part in $published){$archive=Api '/api/archive' @{id=$part.id};$data=$archive.records[0].data;Check ($data.text-ceq$part.text-and$data.message_index-eq$part.message_index-and$data.request_id-ceq$bundleId) 'each message has its own durable archive record'}
 $page=Api '/api/chat/page' @{limit=48};Check ($page.total-eq4-and(Same $page.messages.id $chat.id)) 'history index commits all bundle links atomically and in order'
 Plan (Reply @('不得展示前半条。','也不得展示后半条。'));Plan @{decision='deny';reason='other';guidance=''} $true
 $deniedId='multi-reply-review-denied';$null=Request $deniedId '这是整组拒绝测试。';Idle
 Check (@((Api '/api/chat').messages|Where-Object role -eq assistant).Count-eq3-and(RequestRecord $deniedId).status-eq'rejected') 'semantic denial publishes no partial message'
 $bad=@(
  @{name='empty';value=@{version=1;kind='reply';text=$null;calls=@();messages=@()}},
  @{name='blank';value=(Reply @('不能局部展示。','　'))},
  @{name='conflict';value=(Reply @('不能展示甲。','不能展示乙。') 'conflicting canonical text')},
  @{name='too-many';value=(Reply @('1','2','3','4','5','6','7'))},
  @{name='part-too-long';value=(Reply @('不能局部展示。',('字'*2001)))},
  @{name='total-too-long';value=(Reply @(('甲'*2000),('乙'*2000),('丙'*2000)))},
  @{name='silence-messages';value=@{version=1;kind='silence';text=$null;calls=@();messages=@('不允许偷偷显示。')}}
 )
 foreach($case in $bad){Plan $case.value;Plan @{decision='approve';reason='ok';guidance=''} $true;$requestId='multi-reply-bad-'+$case.name;$null=Request $requestId '这是坏数组的合成检查。';Idle;$request=RequestRecord $requestId;Check ($request.status-eq'rejected'-and!$request.reply_ids-and@((Api '/api/chat').messages|Where-Object role -eq assistant).Count-eq3) ('invalid '+$case.name+' rejects the entire bundle without publishing its valid prefix')}
 $long='旧单条'+('汉'*2499);Plan @{version=1;kind='reply';text=$long;calls=@();presentation=@{speech=$false;bubble=$false}};Plan @{decision='approve';reason='ok';guidance=''} $true
 $longId='multi-reply-legacy-long';$null=Request $longId '保留一条超过两千字的旧格式回复。';Idle
 $last=(Api '/api/chat').messages[-1];$longRecord=RequestRecord $longId
 Check ($last.text-ceq$long-and$last.text.Length-gt2000-and!$last.reply_bundle_id-and$longRecord.reply_ids.Count-eq1-and!$longRecord.message_bundle_authored) 'legacy text above 2000 characters stays one record without invented authored messages'
 $snapshot=@((Api '/api/chat').messages);$history=Api '/api/chat/page' @{limit=48};$requestsBefore=(Invoke-RestMethod ($fixture+'/fixture/health')).calls
 $null=Api '/api/control' @{paused=$true};Stop-Host;$hostOrdinal=2;Start-Host
 Check ((Api '/api/dashboard').paused) 'paused state survives a new host process'
 $restored=@((Api '/api/chat').messages);$restoredHistory=Api '/api/chat/page' @{limit=48}
 Check ((Same $restored.id $snapshot.id)-and(Same $restored.text $snapshot.text)-and(Same $restoredHistory.messages.id $history.messages.id)-and$restoredHistory.total-eq$history.total) 'new process restores every message and history position exactly once'
 Check ((Same (RequestRecord $bundleId).reply_ids $record.reply_ids)-and(Same (RequestRecord $longId).reply_ids $longRecord.reply_ids)) 'saved per-request reply id order survives restart'
 Start-Sleep -Milliseconds 500
 Check ((Invoke-RestMethod ($fixture+'/fixture/health')).calls-eq$requestsBefore-and(Same (Api '/api/chat').messages.id $snapshot.id)) 'restart and duplicate request lookup perform no extra model call and publish no duplicate reply'
 Check ((Api '/api/memories').memories.Count-eq0-and@((Api '/api/dashboard').plugins|Where-Object enabled).Count-eq0) 'fixture creates no memories and enables no external plugin'
 Stop-Host;$completed=$true
 Write-Host "Multi-reply process: $($checks.Count) checks. Evidence: $root"
}catch{$failure=$_.Exception.Message;throw}finally{
 @{completed=$completed;checks=$checks;failure=$failure;executable_sha256=$candidateHash;actual_paid_calls=0;provider='loopback_fixture';character_scope='literal_synthetic_only';production_records_read=$false;root=$root;finished_at=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8
 foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}
 foreach($key in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$savedEnvironment[$key],'Process')}
}