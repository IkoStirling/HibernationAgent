param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
$root=Join-Path $ScratchRoot ('multi-reply-access-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$candidateHash=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
$service=$null;$provider=$null;$checks=[Collections.Generic.List[string]]::new();$completed=$false;$failure=''
$savedEnvironment=@{TEMP=$env:TEMP;TMP=$env:TMP;HIBERNATION_SESSION_KEY=$env:HIBERNATION_SESSION_KEY}
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='multi-reply-access-fixture'
function Check([bool]$Value,[string]$Name){if(!$Value){throw "CHECK FAILED: $Name"};$checks.Add($Name);Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,[string]$Name){$deadline=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$deadline){if(&$Probe){return};Start-Sleep -Milliseconds 35};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$parameters=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer multi-reply-access-fixture'};TimeoutSec=12};if($null-ne$Data){$parameters.Method='Post';$parameters.ContentType='application/json';$parameters.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 75 -Compress))};Invoke-RestMethod @parameters}
function Native($Id,$Name,$Arguments){@{fixture_native_calls=@(@{id=$Id;name=$Name;arguments=$Arguments})}}
function Plan($Items,[bool]$Review=$false){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 75|Set-Content -LiteralPath (Join-Path $providerRoot $(if($Review){'review-plan.json'}else{'plan.json'})) -Encoding utf8}
function Message([string]$Id,[string]$Text,$Responses){
 Plan $Responses;Plan @(@{decision='approve';reason='ok';guidance=''}) $true
 $null=Api '/api/message' @{request_id=$Id;text=$Text};Await {!(Api '/api/chat').busy} 'turn completion'
 $record=(Api '/api/message' @{request_id=$Id;text='idempotent lookup only'}).request
 Check ($record.status-eq'completed'-and!(Api '/api/chat').call_failed) 'synthetic native turn completed'
 Api '/api/trace' @{id=$record.trace_id}
}
function Same($Left,$Right){(ConvertTo-Json -InputObject @($Left) -Compress -Depth 20)-ceq(ConvertTo-Json -InputObject @($Right) -Compress -Depth 20)}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Path $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'" 600') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'loopback provider readiness';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $character=Join-Path $root synthetic-character;New-Item -ItemType Directory -Path $character|Out-Null
 @{schemaVersion=1;version=1;id='multi-reply-access-fixture';displayName='合成角色 A';notice='完全合成的多条授权检查。';identity='只存在于隔离测试中的虚构角色。';attention=@('合成对话');boundaries=@('保留已获准内容的来源，尊重撤销。');character=@{dialogue=@('自然交谈。')}}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character persona.json) -Encoding utf8
 @{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='合成初始';description='隔离授权检查。';next='';minimumEvidence=0});reviewPolicy='不改变阶段。'}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character stages.json) -Encoding utf8
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false;readRoots=@()};$cfg.skills=@{enabled=$false;useBuiltins=$false;root=(Join-Path $root skills);grants=@()}
 $cfg.draftsRoot=Join-Path $root workspace/drafts;$cfg.materialsRoot=Join-Path $root workspace/materials;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.maxStartsPerHour=1000;$cfg.recentMessageCount=12
 $cfg.contextControl=@{enabled=$true;preset='balanced';sessionProjection=$true;sessionMaxTurns=8;sessionBytes=131072}
 $cfg.profiles=@(@{id='mock';label='Mock';enabled=$true;protocol='mock'},@{id='actor';label='Local actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsToolCalls=$true;supportsJsonObject=$true;maxOutputTokens=8192},@{id='review';label='Local reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=0;retainConversationFocus=$false};$configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 75|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root service-error.log)
 Await {$service.Refresh();if($service.HasExited){throw 'Synthetic access host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'synthetic host readiness'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 $suffix=[Guid]::NewGuid().ToString('N');$parts=@(('MR_ACCESS_FIRST_'+$suffix),('MR_ACCESS_SECOND_'+$suffix),('MR_ACCESS_THIRD_'+$suffix))
 $first=Message 'multi-access-first' '生成这三条完全合成的回复。' @((Native bundle ha_turn_reply @{text=$null;messages=$parts;presentation=@{speech=$false;bubble=$false}}))
 $published=@((Api '/api/chat').messages|Where-Object role -eq assistant)
 Check ($first.session_projection.mode-eq'baseline'-and$published.Count-eq3-and(Same $published.text $parts)) 'first projected turn archives three independently authored marker messages'
 $second=Message 'multi-access-derived' '只保存第二条回复的来源，不复述正文。' @((Native read-part ha_memory_DOT_archive_read @{id=$published[1].id}),(Native remember-part ha_memory_DOT_remember @{kind='inference';text='合成第二条来源的派生记忆';source_ids=@($published[1].id)}),(Native finish ha_turn_reply @{text='来源已按回执保留。';presentation=@{speech=$false;bubble=$false}}))
 $second|ConvertTo-Json -Depth 90|Set-Content -LiteralPath (Join-Path $root before-revocation-trace.json) -Encoding utf8
 Check ($second.session_projection.mode-eq'append') 'next native turn actually reuses the approved cached prefix'
 $refs=@($second.session_projection.retained_sources);foreach($part in $published){Check (@($refs|Where-Object {$_.id-ceq$part.id-and$_.kind-eq'assistant_message'-and$_.access-eq'narrative'}).Count-eq1) 'retained cache binds each individual bundle archive record exactly once'}
 $before=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90;$bundle=$null
 foreach($message in $before.messages){if($message.role-ne'assistant'-or$message.content-isnot[string]){continue};try{$item=$message.content|ConvertFrom-Json -Depth 80;if($item.kind-eq'reply'-and$item.messages-and(Same $item.messages $parts)){$bundle=$item}}catch{}}
 Check ($null-ne$bundle-and$bundle.text-ceq($parts-join"`n`n")) 'cached approved assistant turn preserves authored messages and canonical text'
 $memory=@((Api '/api/memories').memories|Where-Object text -eq '合成第二条来源的派生记忆')
 Check ($memory.Count-eq1-and$memory[0].accessible) 'a real source-backed derived memory provides the existing access boundary'
 $null=Api '/api/access' @{id=$memory[0].id;accessible=$false}
 $third=Message 'multi-access-after-revoke' '继续讨论仍有权限的合成内容。' @((Native continue ha_turn_reply @{text='继续看仍然可见的部分。';presentation=@{speech=$false;bubble=$false}}))
 $third|ConvertTo-Json -Depth 90|Set-Content -LiteralPath (Join-Path $root after-revocation-trace.json) -Encoding utf8
 $actor=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw;$review=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw
 Check ($third.session_projection.mode-eq'baseline'-and$third.session_projection.reason-eq'authority_changed') 'revoking one message origin rebuilds the authorized prompt cache'
 Check (!$actor.Contains($parts[1])-and!$review.Contains($parts[1])) 'revoked second-message marker stays out of all next actor and reviewer input'
 Check ($actor.Contains($parts[0])-and$actor.Contains($parts[2])) 'authorized first and third messages remain available after the narrow revocation'
 $original=Api '/api/archive' @{id=$published[1].id};Check ($original.records[0].data.text-ceq$parts[1]) 'source restriction preserves the immutable original second archive record'
 $page=Api '/api/chat/page' @{limit=48};$originalParts=@($page.messages|Where-Object {$_.id-in$published.id})
 Check ((Same $originalParts.id $published.id)-and(Same $originalParts.text $parts)) 'owner history still retains the complete original bundle and message order'
 Check (@((Api '/api/dashboard').plugins|Where-Object enabled).Count-eq0) 'synthetic process enables no external plugin'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'synthetic access host saves and exits cleanly'
 $completed=$true;Write-Host "Multi-reply access: $($checks.Count) checks. Evidence: $root"
}catch{$failure=$_.Exception.Message;throw}finally{
 @{completed=$completed;checks=$checks;failure=$failure;executable_sha256=$candidateHash;actual_paid_calls=0;provider='loopback_fixture';character_scope='literal_synthetic_only';production_records_read=$false;root=$root;finished_at=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8
 foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};foreach($key in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$savedEnvironment[$key],'Process')}
}