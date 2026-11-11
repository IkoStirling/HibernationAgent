param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot,[int]$ExpectedPolicyVersion=4)
$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
$root=Join-Path $ScratchRoot ('receipt-conversation-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$service=$null;$provider=$null;$checks=[Collections.Generic.List[string]]::new();$savedEnvironment=@{TEMP=$env:TEMP;TMP=$env:TMP;HIBERNATION_SESSION_KEY=$env:HIBERNATION_SESSION_KEY}
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='receipt-conversation-local-fixture'
function Check([bool]$Value,[string]$Name){if(!$Value){throw "CHECK FAILED: $Name"};$checks.Add($Name);Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,[string]$Name){$deadline=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$deadline){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$parameters=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer receipt-conversation-local-fixture'};TimeoutSec=12};if($null-ne$Data){$parameters.Method='Post';$parameters.ContentType='application/json';$parameters.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @parameters}
function Call([string]$Id,[string]$Tool,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool=$Tool;args=$Arguments})}}
function Reply([string]$Text){@{version=1;kind='reply';text=$Text;calls=@();presentation=@{speech=$false;bubble=$false}}}
function Plan($Items,[bool]$Review=$false){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $providerRoot $(if($Review){'review-plan.json'}else{'plan.json'})) -Encoding utf8}
function Capture{Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 90}
function Receipt{
 $all=@(foreach($message in (Model-CurrentMessages (Capture))){if($message.role-ne'user'-or$message.content-isnot[string]){continue};try{$frame=$message.content|ConvertFrom-Json -Depth 85;if($frame.tool_results){$frame.tool_results}}catch{}})
 if(!$all.Count){throw 'Missing actual tool receipt'};$all[-1]
}
function Message([string]$Text,$Responses,$ReviewResponses=$null){
 Plan $Responses;if($null-eq$ReviewResponses){$ReviewResponses=@(@{decision='approve';reason='ok';guidance=''})};Plan $ReviewResponses $true
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'message completion'
 Check (!(Api '/api/chat').call_failed) 'local protocol turn completed'
 $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$result=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$result.trace_id}
}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Path $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'" 300') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'loopback provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $character=Join-Path $root synthetic-character;New-Item -ItemType Directory -Path $character|Out-Null
 @{schemaVersion=1;version=1;id='receipt-conversation-fixture';displayName='合成角色 R';notice='完全合成的协议检查。';identity='只存在于这个隔离测试中的虚构角色。';attention=@('合成草稿');boundaries=@('依据真实回执描述动作，可以拒绝或安静。');character=@{dialogue=@('自然交谈。')}}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character persona.json) -Encoding utf8
 @{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='合成初始';description='隔离协议检查。';next='';minimumEvidence=0});reviewPolicy='不改变阶段。'}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $character stages.json) -Encoding utf8
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$false;readRoots=@()};$cfg.skills=@{enabled=$false;useBuiltins=$false;root=(Join-Path $root skills);grants=@()}
 $cfg.draftsRoot=Join-Path $root workspace/drafts;$cfg.materialsRoot=Join-Path $root workspace/materials;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.contextControl=@{enabled=$false};$cfg.maxStartsPerHour=1000
 $cfg.profiles=@(@{id='mock';label='Mock';enabled=$true;protocol='mock'},@{id='actor';label='Local actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true},@{id='review';label='Local reviewer';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true});$cfg.review=@{enabled=$true;profileId='review';maxRewrites=1;retainConversationFocus=$false}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root service-error.log)
 Await {if($service.HasExited){throw 'Synthetic host exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'synthetic host';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 $original="# 正文`n桌角放着一只蓝杯。`n# 更正记录`n最初写的是蓝杯。`n";$file=Join-Path $cfg.draftsRoot scope.md
 $null=Message '写下这份合成草稿。' @((Call write workspace.write @{path='scope.md';content=$original}),(Reply '写好了。'));Check ((Receipt).ok-and[IO.File]::ReadAllText($file)-ceq$original) 'successful write matches actual synthetic file'
 $null=Message '先读取，再核对正文。' @((Call read workspace.read @{mount='drafts';path='scope.md'}),(Reply '正文还是蓝杯。'));$read=Receipt;Check ($read.ok-and$read.result.eof-and$read.result.content-ceq$original) 'full read supplies content and its exact digest'
 $null=Message '只把正文改成灰杯，原更正记录保留。' @((Call edit workspace.edit @{path='scope.md';expected_sha256=$read.result.content_sha256;old_text='桌角放着一只蓝杯。';new_text='桌角放着一只灰杯。'}),(Reply '正文改成灰杯了。'))
 $after=[IO.File]::ReadAllText($file);Check ((Receipt).ok-and(Receipt).result.replacements-eq1-and$after.Contains('桌角放着一只灰杯。')-and$after.Contains('最初写的是蓝杯。')) 'one local edit preserves the correction record containing the old color'
 $null=Message '整份文件里蓝杯这个词还留着吗？' @((Call full workspace.read @{mount='drafts';path='scope.md'}),(Reply '还在更正记录里，正文是灰杯。'));$full=Receipt;Check ($full.ok-and$full.result.content-ceq$after-and(Api '/api/chat').messages[-1].text-ceq'还在更正记录里，正文是灰杯。') 'scope-correct answer stays unchanged while actual full read retains old content'
 $short='这次没读到，还不能确定它是不是不存在。';$null=Message '看看不存在的合成路径。' @((Call missing workspace.read @{mount='drafts';path='missing-fixture.md'}),(Reply $short));$denied=Receipt
 Check (!$denied.ok-and$denied.error.code-eq'workspace_read_denied'-and$denied.guidance) 'failed receipt and internal recovery guidance remain available'
 Check ((Api '/api/chat').messages[-1].text-ceq$short) 'short failure reply receives no automatic apology or implementation appendix'
 $technical='读取返回 workspace_read_denied，单凭这个不能确认文件不存在。';$null=Message '这次明确告诉我错误码与能确认的范围。' @((Call missing2 workspace.read @{mount='drafts';path='missing-fixture.md'}),(Reply $technical));Check ((Api '/api/chat').messages[-1].text-ceq$technical) 'explicit technical answer preserves code and uncertainty unchanged'
 $catch='我接住的是这个假想的球。';$null=Message '玩个语言游戏：我假装抛来一个球，你接住它会怎么说？只回答台词。' @((Reply $catch));Check ((Api '/api/chat').messages[-1].text-ceq$catch) 'appropriate catch wording is not subject to a keyword deletion filter'
 $natural='酸酸的，我想慢一点吃。';$pledge='至于味道，我不替它先编好。'
 $null=Message '只聊一下这种想象中的小果子。' @((Reply $pledge),(Reply $natural)) @(@{decision='rewrite';reason='persona';guidance='回到眼前想表达的具体意思。'},@{decision='approve';reason='ok';guidance=''})
 Check ((Api '/api/chat').messages[-1].text-ceq$natural-and@((Api '/api/chat').messages|Where-Object text -eq $pledge).Count-eq0) 'role-direction rewrite publishes only the revised utterance and never the rejected candidate'
 $promise='我答应。';$null=Message '做一句台词练习，只说我答应，不安排实际任务。' @((Reply $promise))
 Check ((Api '/api/chat').messages[-1].text-ceq$promise) 'an appropriate promise remains unchanged; no blanket pledge word filter'
 $actor=Capture;$reviewCapture=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 90
 $actorEvent=Model-CurrentEvent $actor;$reviewEvent=Model-ReviewEvent $reviewCapture
 Check ($actorEvent.conversation_delivery.policy_version-eq$ExpectedPolicyVersion-and$reviewEvent.conversation_delivery.policy_version-eq$ExpectedPolicyVersion) 'actor and reviewer share expected conversation policy version'
 if($ExpectedPolicyVersion-ge4){Check ($actorEvent.conversation_delivery.review_purpose-eq'role_direction'-and$reviewEvent.conversation_delivery.subjective_experience-eq'character_world') 'actor and director share the current character-world delivery contract'}
 elseif($ExpectedPolicyVersion-ge3){$marker='当前正文、整份文件和历史记录是不同范围';Check ($actor.messages[0].content.Contains($marker)-and$reviewCapture.messages[0].content.Contains($marker)) 'candidate actor and reviewer both receive the same scoped communication rule'}
 Check ([IO.File]::ReadAllText($file)-ceq$after-and(Api '/api/memories').memories.Count-eq0) 'later replies neither rewrite draft history nor silently create memories'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'synthetic host saves and exits cleanly'
 @{completed=$true;checks=$checks;expected_policy_version=$ExpectedPolicyVersion;actual_paid_calls=0;provider='loopback_fixture';character_scope='literal_synthetic_only';production_records_read=$false;naturalness_validated=$false;root=$root}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root acceptance.json) -Encoding utf8
 Write-Host "Receipt conversation process: $($checks.Count) checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};foreach($key in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$savedEnvironment[$key],'Process')}}
