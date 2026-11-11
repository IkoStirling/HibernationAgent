param(
 [string]$ProjectRoot=(Split-Path $PSScriptRoot -Parent),
 [string]$BuildRoot='',
 [Parameter(Mandatory)][string]$LabRoot,
 [Parameter(Mandatory)][string]$Episode,
 [string]$SourceConfig='',
 [string]$ProfileId='',
 [string]$NodeExecutable=$env:HA_NODE_RUNTIME,
 [string]$BudgetLedger='',
 [string]$ExternalRelayFile='',
 [ValidateSet('baseline','updated','probe','contrast')][string]$RelayBatch='baseline',
 [switch]$PrivateReports,
 [switch]$EnableCognition,
 [switch]$SyntheticCharacter,
 [switch]$PersonaOnly,
 [string]$DeadlineUtc='2026-10-28T00:37:47Z',
 [switch]$Serve,
 [string]$ServeUntilUtc='2026-10-27T23:52:47Z',
 [switch]$Live,
 [switch]$RefreshRuntime
)
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/skills-release}
if(!$SourceConfig){$SourceConfig=Join-Path $ProjectRoot $(if($Live){'config/local.json'}else{'config/example.json'})}
if(!$NodeExecutable){throw 'Set HA_NODE_RUNTIME or NodeExecutable to Node.js 24 or newer'}
$LabRoot=[IO.Path]::GetFullPath($LabRoot)
$allowed=[IO.Path]::GetFullPath((Join-Path $ProjectRoot artifacts))+[IO.Path]::DirectorySeparatorChar
if(!$LabRoot.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'LabRoot must be a dedicated directory inside project artifacts'}
if(!$BudgetLedger){$BudgetLedger=Join-Path $ProjectRoot artifacts/personality-growth-20261027/budget.json}
$BudgetLedger=[IO.Path]::GetFullPath($BudgetLedger)
if(!$BudgetLedger.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Experiment ledger must stay inside project artifacts; all contrast branches share this ledger'}
$steps=Get-Content -LiteralPath $Episode -Raw|ConvertFrom-Json -AsHashtable
if($steps.version-ne1-or!$steps.title-or$steps.turns-isnot[Array]-or$steps.turns.Count-lt$(if($Serve){0}else{1})-or$steps.turns.Count-gt20){throw 'Episode requires version 1, title and 1..20 meaningful turns; a continuing controller can resume with zero turns'}
if($steps.ContainsKey('referenceReading')-and$steps.referenceReading-isnot[bool]){throw 'referenceReading must be an explicit boolean'}
if($SyntheticCharacter-and$steps.referenceReading){throw 'Synthetic-only trials cannot import reference readings'}
if($PersonaOnly-and($SyntheticCharacter-or$steps.referenceReading-or!$PrivateReports)){throw 'PersonaOnly requires private reports, no reference readings and no synthetic persona replacement'}
if($steps.opportunitySeconds-and(!$steps.autonomy-or$steps.opportunitySeconds-lt1-or$steps.opportunitySeconds-gt45)){throw 'An independent activity opportunity requires autonomy and 1..45 seconds'}
foreach($step in $steps.turns){if(!$step.text-or$step.text.Length-gt3000){throw 'Episode text must be 1..3000 characters'};if($step.settleSeconds-and($step.settleSeconds-lt0-or$step.settleSeconds-gt30)){throw 'Activity settlement is limited to 30 seconds'}}
if($Serve){$serveUntil=[DateTime]::Parse($ServeUntilUtc).ToUniversalTime();if($serveUntil-le[DateTime]::UtcNow-or$serveUntil-gt[DateTime]::Parse($DeadlineUtc).ToUniversalTime()){throw 'Controller must end inside the authorized work window'}}
$source=Get-Content -LiteralPath $SourceConfig -Raw|ConvertFrom-Json -AsHashtable
$cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
if($Live){
 if(!$ProfileId){$ProfileId=@($source.profiles|Where-Object {$_.enabled-and$_.protocol-ne'mock'-and$_.id-ne$source.review.profileId-and$_.origin-eq'https://api.deepseek.com'})[0].id}
 $actor=@($source.profiles|Where-Object {$_.id-eq$ProfileId-and$_.enabled-and$_.protocol-ne'mock'})[0]
 $reviewer=@($source.profiles|Where-Object {$_.id-eq$source.review.profileId-and$_.enabled-and$_.protocol-ne'mock'})[0]
 if(!$actor-or!$reviewer){throw 'Configured actor and reviewer are required for live personality work'}
 $cfg.profiles=@(@($cfg.profiles|Where-Object protocol -eq mock)[0],$actor,$reviewer);$cfg.review=@{enabled=$true;profileId=$reviewer.id;maxRewrites=2;retainConversationFocus=$true}
}else{$cfg.profiles=@($cfg.profiles|Where-Object protocol -eq mock);$ProfileId='mock';$cfg.review=@{enabled=$false}}
New-Item -ItemType Directory -Force $LabRoot|Out-Null
# One runner owns the continuing archive. A crash leaves a visible lock for review.
$runLock=Join-Path $LabRoot runner.lock
$lockStream=[IO.File]::Open($runLock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
$service=$null;$desktop=$null;$relay=$null;$relayOrigin='';$relayCapability='';$copies=@();$controller=$null;$controllerPath=''
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;HIBERNATION_SESSION_KEY=$env:HIBERNATION_SESSION_KEY}
$session='personality-lab-'+[Guid]::NewGuid().ToString('N');$report=$null
function Api($Path,$Data=$null){
 $parameters=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer '+$session};TimeoutSec=12}
 if($null-ne$Data){$parameters.Method='Post';$parameters.ContentType='application/json';$parameters.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))}
 Invoke-RestMethod @parameters
}
function AwaitIdle{
 $end=[DateTime]::UtcNow.AddSeconds(115)
 while([DateTime]::UtcNow-lt$end){if($service.HasExited){throw 'Lab backend exited'};if(!(Api '/api/chat').busy){return};Start-Sleep -Milliseconds 100}
 throw 'Lab turn deadline reached'
}
function Save-LabReport{
 $report.updated_utc=[DateTime]::UtcNow.ToString('O');$json=$report|ConvertTo-Json -Depth 85
 if(!$PrivateReports){$json|Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM;return}
 Add-Type -AssemblyName System.Security
 $plain=[Text.Encoding]::UTF8.GetBytes($json)
 try{$sealed=[Security.Cryptography.ProtectedData]::Protect($plain,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);$temporary=$reportPath+'.pending';[IO.File]::WriteAllBytes($temporary,$sealed);[IO.File]::Move($temporary,$reportPath,$true)}finally{[Array]::Clear($plain,0,$plain.Length)}
 @{version=1;started_utc=$report.started_utc;updated_utc=$report.updated_utc;live=[bool]$Live;turns=$report.turns.Count;failed_turns=@($report.turns|Where-Object failed).Count;encrypted_report=$true;binary_sha256=$report.binary_sha256}|ConvertTo-Json|Set-Content -LiteralPath ($reportPath+'.metadata.json') -Encoding utf8NoBOM
}
function Run-LabTurn($Step,[string]$RequestIdentity=''){
 if($Serve-and[DateTime]::UtcNow-ge$serveUntil){throw 'Controller work window ended'}
 if($Step.text-isnot[string]-or![bool]$Step.text.Trim()-or$Step.text.Length-gt3000){throw 'Invalid dialogue input'}
 if($Step.settleSeconds-and($Step.settleSeconds-lt0-or$Step.settleSeconds-gt30)){throw 'Invalid settlement interval'}
 $requestId=if($RequestIdentity){$RequestIdentity}else{[Guid]::NewGuid().ToString('N')};$before=Api '/api/state';$watch=[Diagnostics.Stopwatch]::StartNew()
 $null=Api '/api/message' @{request_id=$requestId;text=$Step.text};AwaitIdle
 if($Step.settleSeconds){$end=[DateTime]::UtcNow.AddSeconds($Step.settleSeconds);do{$activity=Api '/api/activities/status';if($activity.queued-eq0-and$null-eq$activity.active){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end)}
 $chat=Api '/api/chat';$state=Api '/api/state';$job=@($state.jobs|Where-Object id -eq chat)[0]
 $trace=$null;if($job.result){$jobResult=$job.result|ConvertFrom-Json;if($jobResult.trace_id){$trace=Api '/api/trace' @{id=$jobResult.trace_id}}}
 $reply=$null;$last=@($chat.messages|Where-Object role -eq assistant|Select-Object -Last 1)
 if($last.Count){$record=Api '/api/archive' @{id=$last[0].id};if($record.records[0].data.request_id-eq$requestId){$reply=$last[0].text}}
 $row=@{request_id=$requestId;input=$Step.text;reply=$reply;failed=$chat.call_failed;call_error=$chat.call_error;elapsed_ms=$watch.ElapsedMilliseconds;state_before=$before.continuous_state;state_after=$state.continuous_state;activities=(Api '/api/activities/status');trace=$trace}
 $report.turns+=,$row;Save-LabReport
 if($PrivateReports){Write-Output ('Private turn completed: '+$requestId+'; reply_present='+[bool]$reply+'; failed='+$chat.call_failed)}else{Write-Output ($Step.text+"`n小爱："+$reply)}
 if($chat.call_failed){throw 'Episode paused after failed turn; inspect its trace before continuing the same archive'}
}
function Consider-OwnWork{
 if($Serve-and[DateTime]::UtcNow-ge$serveUntil){throw 'Controller work window ended'}
 $old=@((Api '/api/state').jobs|Where-Object id -eq solitude.once);$token=if($old.Count){$old[0].run_token}else{0}
 $null=Api '/api/solitude' @{};$end=[DateTime]::UtcNow.AddSeconds(115)
 do{
  if($service.HasExited){throw 'Lab backend exited'}
  $job=@((Api '/api/state').jobs|Where-Object id -eq solitude.once)[0]
  if($job.run_token-gt$token-and$job.state-in@('succeeded','failed','paused')){break}
  if([DateTime]::UtcNow-ge$end){throw 'Private consideration deadline reached'};Start-Sleep -Milliseconds 100
 }while($true)
 $outcome=if($job.result){$job.result|ConvertFrom-Json}else{$null};$trace=if($outcome.trace_id){Api '/api/trace' @{id=$outcome.trace_id}}else{$null}
 $report.private_consideration=@{status=$outcome.status;job_state=$job.state;trace=$trace};Save-LabReport
 Write-Output ('Own-work consideration: '+$outcome.status)
}
function Lab-Snapshot{
 $state=Api '/api/state';$surfaces=@((Api '/api/capabilities').plugins|Where-Object {$_.id-in@('widget','habitat','pocket','atelier','rhythm','reading')})
 if($PrivateReports){$activity=Api '/api/activities/status';$mind=Api '/api/mind/status';return @{observed_utc=[DateTime]::UtcNow.ToString('O');backend_pid=$service.Id;busy=$state.busy;activity_queued=$activity.queued;activity_active=($null-ne$activity.active);activity_last_error=$activity.last_error;mind=$mind;circadian=(Api '/api/rhythm/status');report_path=$reportPath;turn_count=$report.turns.Count;dialogue_message_count=@((Api '/api/chat').messages).Count;persona_version=$state.persona_version;stage_id=$state.stage.id}}
  return @{observed_utc=[DateTime]::UtcNow.ToString('O');backend_pid=$service.Id;busy=$state.busy;continuous_state=$state.continuous_state;activities=(Api '/api/activities/status');surfaces=$surfaces;report_path=$reportPath;turn_count=$report.turns.Count;dialogue_message_count=@((Api '/api/chat').messages).Count;persona_version=$state.persona_version;stage_id=$state.stage.id}
}
try{
 $marker=Join-Path $LabRoot .personality-lab.json
 $mode=if($Live){'live'}else{'mock'}
 if(Test-Path -LiteralPath $marker){$identity=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json;if($identity.version-ne1-or$identity.mode-ne$mode-or$identity.profile_id-ne$ProfileId){throw 'Archive identity mismatch; do not mix mock/live or character connections'}}
 else{@{version=1;mode=$mode;profile_id=$ProfileId;created_utc=[DateTime]::UtcNow.ToString('O');production_archive_read=$false;purpose='continuous isolated personality growth'}|ConvertTo-Json|Set-Content -LiteralPath $marker -Encoding utf8}
 $runtime=Join-Path $LabRoot runtime
 if(!(Test-Path -LiteralPath $runtime)-or$RefreshRuntime){
  New-Item -ItemType Directory -Force $runtime|Out-Null
  New-Item -ItemType Directory -Force (Join-Path $runtime plugins)|Out-Null
  Copy-Item -LiteralPath (Join-Path $BuildRoot hibernation-agent.exe) -Destination $runtime -Force
  foreach($dll in Get-ChildItem -LiteralPath $BuildRoot -Filter *.dll -File){Copy-Item -LiteralPath $dll.FullName -Destination $runtime -Force}
  foreach($pluginId in @('widget','atelier','habitat','pocket','reading','rhythm')){Copy-Item -LiteralPath (Join-Path $BuildRoot ('plugins/'+$pluginId)) -Destination (Join-Path $runtime plugins) -Recurse -Force}
 }
 $character=Join-Path $LabRoot character
 if($Serve-and!(Test-Path -LiteralPath (Join-Path $runtime plugins/rhythm/plugin.json))){throw 'RefreshRuntime is required before enabling this controller'}
 if($SyntheticCharacter){
  # Literal synthetic definitions: do not read/copy the user's persona, novel
  # reference files, existing memories, or ordinary character directory.
  $syntheticPersona=@{schemaVersion=1;version=1;id='synthetic-continuity-fixture';displayName='测试角色 R';notice='完全合成的隔离技术试验。';identity='完全虚构的通用桌面实验角色，没有真实人物或作者设定。';attention=@('合成谜题','物件之间的因果','未完成的小计划');boundaries=@('区分合成经历、推测和未来计划。','仅按实际工具回执描述操作结果。','可以安静、拒绝或修正判断，不要求讨好，也不预设亲密关系。');character=@{dialogue=@('使用自然、简短的中文。','未被问及时不复述程序和实验设置。')}}
  $syntheticStages=@{schemaVersion=1;version=1;initial='initial';stages=@(@{id='initial';name='合成初始阶段';description='只积累独立试验中的合成经历。';next='';minimumEvidence=0});reviewPolicy='本实验不自动改变阶段。'}
  New-Item -ItemType Directory -Force $character|Out-Null
  foreach($pair in @(@{name='persona.json';value=$syntheticPersona},@{name='stages.json';value=$syntheticStages})){
   $target=Join-Path $character $pair.name;$expected=$pair.value|ConvertTo-Json -Depth 20 -Compress
   if(Test-Path -LiteralPath $target){$actual=Get-Content -LiteralPath $target -Raw;if(![Text.Json.Nodes.JsonNode]::DeepEquals([Text.Json.Nodes.JsonNode]::Parse([string]$actual),[Text.Json.Nodes.JsonNode]::Parse([string]$expected))){throw 'Synthetic trial character changed or belongs to another scope'}}
   else{$expected|Set-Content -LiteralPath $target -Encoding utf8NoBOM}
  }
 }elseif($PersonaOnly){
  # The user-authorized persona is separate from formal memories, workspace
  # contents and referenced novel material. Never copy a character directory.
  New-Item -ItemType Directory -Force $character|Out-Null
  foreach($leaf in @('persona.json','stages.json')){
   $original=Join-Path $ProjectRoot ('character/'+$leaf);$target=Join-Path $character $leaf
   if((Get-Item -LiteralPath $original).Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked persona input rejected'}
   if(Test-Path -LiteralPath $target){if((Get-FileHash -LiteralPath $target).Hash-ne(Get-FileHash -LiteralPath $original).Hash){throw 'Isolated persona changed; use a fresh trial directory'}}
   else{Copy-Item -LiteralPath $original -Destination $target}
  }
  if(@(Get-ChildItem -LiteralPath $character -Force|Where-Object {$_.PSIsContainer-or$_.Name-notin@('persona.json','stages.json')}).Count){throw 'PersonaOnly character directory contains unapproved material'}
 }elseif(!(Test-Path -LiteralPath $character)){Copy-Item -LiteralPath (Join-Path $ProjectRoot character) -Destination $character -Recurse}
 $cfg.dataRoot=Join-Path $LabRoot data;$cfg.draftsRoot=Join-Path $cfg.dataRoot drafts;$cfg.materialsRoot=Join-Path $cfg.dataRoot materials
 $cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $runtime plugins;$cfg.port=0
 $cfg.frontend.startupAnimation=$false;$cfg.computer=@{enabled=$false;allowScreenCapture=$false;readRoots=@()}
 $cfg.plugins=@{
  widget=@{enabled=$true;skin='asterlan';size=168;animationMode='sequence';autonomousMode=$false;roam=$false;showBubble=$false;respectSystemMotion=$false;reactionCacheEnabled=$false}
  atelier=@{enabled=$true;maximumPieces=48}
  habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;maxObjects=6;lifetimeMinSeconds=600;lifetimeMaxSeconds=1200}
  pocket=@{enabled=$true;scrambleSteps=3;maximumMoves=24}
  reading=@{enabled=[bool]$steps.referenceReading;autonomousReading=([bool]$steps.referenceReading-and([bool]$steps.autonomy-or[bool]$Serve));language='zh';maxRequestsPerHour=12;useSystemProxy=$true}
 }
 $cfg.maxStartsPerHour=600;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=[bool]$steps.considerOwnWork
 if($EnableCognition){$cfg.cognition=$source.cognition;if(!$cfg.cognition){$cfg.cognition=@{enabled=$true}}else{$cfg.cognition.enabled=$true};$cfg.contextControl=$source.contextControl}
 if($steps.autonomy){$cfg.plugins.widget.autonomousMode=$true;$cfg.plugins.widget.roam=$true;$cfg.plugins.widget.autonomyIdleSeconds=600;$cfg.plugins.widget.autonomyRefillSeconds=600}
 if($steps.opportunitySeconds){$cfg.plugins.widget.autonomyIdleSeconds=15;$cfg.plugins.widget.autonomyRefillSeconds=60}
 if($Serve){
  $cfg.plugins.widget.autonomousMode=$true;$cfg.plugins.widget.roam=$true;$cfg.plugins.widget.autonomyIdleSeconds=45;$cfg.plugins.widget.autonomyRefillSeconds=180
  $cfg.plugins.rhythm=@{enabled=$true;automaticRest=$false}
  $cfg.solitudeIntervalMs=900000;$cfg.idleBeforeSolitudeMs=120000;$cfg.reflectOnActivities=$true
 }
 $cfg.maxToolRounds=8;$cfg.skills=@{enabled=$true;useBuiltins=$true;root=(Join-Path $ProjectRoot skills);grants=@()}
 if($SyntheticCharacter-or$PersonaOnly){$cfg.skills=@{enabled=$false;useBuiltins=$false;root=(Join-Path $LabRoot skills);grants=@()}}
 if($EnableCognition){
  $cfg.maxToolRounds=if($source.maxToolRounds){$source.maxToolRounds}else{6}
  $cfg.backgroundBudget=@{enabled=$true;maxCallsPerDay=1000;maxInputTokensPerDay=16000000}
  if($Live){$cfg.review=@{};foreach($key in $source.review.Keys){$cfg.review[$key]=$source.review[$key]}}
 }
 $secretRoot=Join-Path $cfg.dataRoot runtime/secrets;New-Item -ItemType Directory -Force $secretRoot|Out-Null
 $runRoot=Join-Path $LabRoot ('episodes/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Force $runRoot|Out-Null
 $reportPath=Join-Path $runRoot $(if($PrivateReports){'report.dpapi'}else{'report.json'})
 $report=@{version=1;title=$steps.title;started_utc=[DateTime]::UtcNow.ToString('O');live=[bool]$Live;production_archive_read=$false;continuing_archive=$true;binary_sha256=(Get-FileHash -LiteralPath (Join-Path $runtime hibernation-agent.exe) -Algorithm SHA256).Hash;persona_sha256=(Get-FileHash -LiteralPath (Join-Path $character persona.json) -Algorithm SHA256).Hash;turns=@()}
 $report.character_scope=$(if($SyntheticCharacter){'literal_synthetic_only'}else{'application_persona'});$report.external_reference_reading=[bool]$steps.referenceReading
 $report.persona_only_scope=[bool]$PersonaOnly
 if($Live){
  if([DateTime]::UtcNow-ge[DateTime]::Parse($DeadlineUtc).ToUniversalTime()){throw 'Authorized work window has ended'}
  if($ExternalRelayFile){
   # Preserve the explicit UTC suffix. PowerShell's automatic DateTime parsing
   # followed by Parse(object) loses Kind and shifts this deadline by local TZ.
   $shared=Get-Content -LiteralPath $ExternalRelayFile -Raw|ConvertFrom-Json -AsHashtable -DateKind String
   if($shared.version-ne1-or[DateTime]::Parse($shared.deadline).ToUniversalTime()-lt[DateTime]::Parse($DeadlineUtc).ToUniversalTime()){throw 'Shared relay deadline mismatch'}
   $relay=Get-Process -Id $shared.pid -ErrorAction Stop;$relayOrigin=$shared.origin
   foreach($profile in $cfg.profiles){if($profile.protocol-eq'mock'){continue};$role=if($profile.id-eq$source.review.profileId){'review'}else{'actor'};$route=$shared.routes[$RelayBatch+'-'+$role];if(!$route){throw 'Shared relay route missing'};$profile.origin=$relayOrigin;$profile.basePath=$route.basePath;$profile.useSystemProxy=$false;$profile.proxyOrigin='';$protectedFile=Join-Path $source.dataRoot ('runtime/secrets/'+$profile.id+'.dpapi');if(Test-Path -LiteralPath $protectedFile){$destination=Join-Path $secretRoot ($profile.id+'.dpapi');Copy-Item -LiteralPath $protectedFile -Destination $destination;$copies+=$destination}}
  }else{
  $relayCapability=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24)).ToLowerInvariant();$routes=@{}
  foreach($profile in $cfg.profiles){
   if($profile.protocol-eq'mock'){continue}
   $routes[$profile.id]=@{origin=$profile.origin;basePath=$profile.basePath}
   $profile.useSystemProxy=$false;$profile.proxyOrigin=''
   $protectedFile=Join-Path $source.dataRoot ('runtime/secrets/'+$profile.id+'.dpapi')
   if(Test-Path -LiteralPath $protectedFile){$destination=Join-Path $secretRoot ($profile.id+'.dpapi');Copy-Item -LiteralPath $protectedFile -Destination $destination;$copies+=$destination}
  }
  $relayConfig=Join-Path $runRoot relay.json;$relayReady=Join-Path $runRoot relay-ready.json
  $relayDeadline=if($Serve){$serveUntil.ToString('O')}else{$DeadlineUtc}
  @{ledger=$BudgetLedger;maxCalls=4000;maxTokens=22000000;routes=$routes;capability=$relayCapability;deadline=$relayDeadline}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $relayConfig -Encoding utf8NoBOM
  $relay=Start-Process -FilePath $NodeExecutable -ArgumentList @(('"'+(Join-Path $ProjectRoot tools/PersonalityBudget.mjs)+'"'),('"'+$relayConfig+'"'),('"'+$relayReady+'"')) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $runRoot relay-error.log)
  $end=[DateTime]::UtcNow.AddSeconds(12);while(!(Test-Path -LiteralPath $relayReady)){if($relay.HasExited-or[DateTime]::UtcNow-ge$end){throw 'Budget relay did not start'};Start-Sleep -Milliseconds 100}
  $relayOrigin='http://127.0.0.1:'+((Get-Content -LiteralPath $relayReady -Raw|ConvertFrom-Json).port)
  foreach($profile in $cfg.profiles){if($profile.protocol-ne'mock'){$profile.origin=$relayOrigin;$profile.basePath='/'+$relayCapability+'/'+$profile.id}}
  }
 }
 $configPath=Join-Path $runRoot config.json;$ready=Join-Path $runRoot ready.json
 $cfg|ConvertTo-Json -Depth 70|Set-Content -LiteralPath $configPath -Encoding utf8NoBOM
 & (Join-Path $runtime hibernation-agent.exe) --config $configPath --check-config
 if($LASTEXITCODE-ne0){throw 'Lab configuration preflight failed'}
 $env:TEMP=Join-Path $LabRoot tmp;$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null;$env:HIBERNATION_SESSION_KEY=$session
 Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
 $desktop=[PrivateDesktop]::new((Join-Path $runtime hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$service=$desktop.Child
 $end=[DateTime]::UtcNow.AddSeconds(30);while(!(Test-Path -LiteralPath $ready)){if($service.HasExited-or[DateTime]::UtcNow-ge$end){throw 'Continuing lab backend startup failed'};Start-Sleep -Milliseconds 100}
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $dashboard=Api '/api/dashboard';if($dashboard.health-ne'ok'){throw ('Lab health: '+$dashboard.health+'; '+$dashboard.plugin_issue)}
 # Initialization has already loaded the selected credentials into the child
 # process environment. A continuing session need not retain their disk copies.
 foreach($copy in $copies){Remove-Item -LiteralPath $copy};$copies=@();$report.connection_copies_removed_after_startup=$true
 $null=Api '/api/model' @{id=$ProfileId}
 foreach($step in $steps.turns){Run-LabTurn $step}
 if($steps.opportunitySeconds){
  # One bounded chance to pursue something, not a timed stability/load run.
  $initial=Api '/api/activities/status';$lastIds=@($initial.recent.id);$end=[DateTime]::UtcNow.AddSeconds($steps.opportunitySeconds);$choice=$false
  do{
   $activity=Api '/api/activities/status'
   if($activity.active-or$activity.queued-gt0-or$activity.generating){$choice=$true}
   $new=@($activity.recent|Where-Object {$_.id-notin$lastIds})
   if($choice-and!$activity.generating-and!$activity.active-and$activity.queued-eq0){break}
   Start-Sleep -Milliseconds 200
  }while([DateTime]::UtcNow-lt$end)
  $report.independent_activity=@{bounded_seconds=$steps.opportunitySeconds;choice_observed=$choice;status=$activity;actual_new_outcomes=$new;surfaces=(Api '/api/capabilities').plugins}
  Save-LabReport
  Write-Output ('Independent activity: choice='+$choice+', outcomes='+$new.Count+', error='+$activity.last_error)
 }
 if($steps.considerOwnWork){Consider-OwnWork}
 if($Serve){
  $inbox=Join-Path $LabRoot inbox;$outbox=Join-Path $LabRoot outbox;$processed=Join-Path $LabRoot processed
  New-Item -ItemType Directory -Force $inbox,$outbox,$processed|Out-Null
  $controllerId=[Guid]::NewGuid().ToString('N');$controllerPath=Join-Path $LabRoot controller.json
  $controller=@{version=1;controller_id=$controllerId;running=$true;pid=$PID;process_started_utc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('O');backend_pid=$service.Id;backend_started_utc=$service.StartTime.ToUniversalTime().ToString('O');deadline_utc=$serveUntil.ToString('O');report_path=$reportPath}
  $controller|ConvertTo-Json|Set-Content -LiteralPath $controllerPath -Encoding utf8NoBOM
  Write-Output ('Continuing private lab is ready for file commands: '+$controllerPath)
  $stopRequested=$false
  while(!$stopRequested-and[DateTime]::UtcNow-lt$serveUntil){
   $service.Refresh();if($service.HasExited){throw 'Continuing lab backend exited'}
   if($Live){$relay.Refresh();if($relay.HasExited){throw 'Continuing lab budget relay exited'}}
   $commandFile=Get-ChildItem -LiteralPath $inbox -Filter *.json -File|Sort-Object CreationTimeUtc,Name|Select-Object -First 1
   if(!$commandFile){Start-Sleep -Milliseconds 250;continue}
   if($commandFile.BaseName-notmatch'^[a-f0-9]{32}$'-or($commandFile.Attributes-band[IO.FileAttributes]::ReparsePoint)-or$commandFile.Length-gt65536){throw 'Unsafe controller command file'}
   $responsePath=Join-Path $outbox $commandFile.Name
   if(!(Test-Path -LiteralPath $responsePath)){
    try{
     $command=Get-Content -LiteralPath $commandFile.FullName -Raw|ConvertFrom-Json -AsHashtable -Depth 30
     if($command.controller_id-ne$controllerId){throw 'Stale controller'}
     $allowed=@('controller_id','action');if($command.action-eq'say'){$allowed+='text'}elseif($command.action-eq'episode'){$allowed+='turns'}
     foreach($key in $command.Keys){if($key-notin$allowed){throw 'Unknown controller field'}}
     switch($command.action){
      'say' {Run-LabTurn @{text=$command.text} $commandFile.BaseName;$result=@{reply=$report.turns[-1].reply;failed=$report.turns[-1].failed;report_path=$reportPath};if($PrivateReports){$result=@{reply_emitted=[bool]$report.turns[-1].reply;reply_characters=([string]$report.turns[-1].reply).Length;failed=$report.turns[-1].failed;report_path=$reportPath}}}
      'episode' {if($command.turns-isnot[Array]-or$command.turns.Count-lt1-or$command.turns.Count-gt12){throw 'Invalid conversation block'};$index=0;foreach($step in $command.turns){Run-LabTurn $step ($commandFile.BaseName+'-'+$index);++$index};$result=@{turn_count=$report.turns.Count;report_path=$reportPath}}
      'snapshot' {$result=Lab-Snapshot}
      'consider' {Consider-OwnWork;$result=@{status=$report.private_consideration.status;report_path=$reportPath}}
      'rebuild_dialogue' {$result=Api '/api/dialogue/rebuild' @{};$report.interventions=@($report.interventions|Where-Object {$null-ne$_})+@(@{kind='working_dialogue_projection';at_utc=[DateTime]::UtcNow.ToString('O');receipt=$result});Save-LabReport}
      'wake' {$result=Api '/api/rhythm/wake' @{};$report.interventions=@($report.interventions|Where-Object {$null-ne$_})+@(@{kind='administrator_wake';at_utc=[DateTime]::UtcNow.ToString('O');receipt=$result});Save-LabReport}
      'stop' {$stopRequested=$true;$result=@{stopping=$true;report_path=$reportPath}}
      default {throw 'Unknown controller action'}
     }
     $response=@{ok=$true;result=$result}
    }catch{$response=@{ok=$false;error='lab_command_failed';report_path=$reportPath}} # Never serialize exception/header objects.
    $partial=$responsePath+'.partial';$response|ConvertTo-Json -Depth 85|Set-Content -LiteralPath $partial -Encoding utf8NoBOM;Move-Item -LiteralPath $partial -Destination $responsePath
   }
   Move-Item -LiteralPath $commandFile.FullName -Destination (Join-Path $processed $commandFile.Name)
  }
 }
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(12000)}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($desktop){$desktop.Dispose()}
 foreach($copy in $copies){Remove-Item -LiteralPath $copy -ErrorAction SilentlyContinue}
 if($relay-and!$ExternalRelayFile){$relay.Refresh();if(!$relay.HasExited){try{$null=Invoke-RestMethod -Method Post -Uri ($relayOrigin+'/'+$relayCapability+'/stop') -TimeoutSec 10;$null=$relay.WaitForExit(5000)}catch{};$relay.Refresh();if(!$relay.HasExited){Stop-Process -Id $relay.Id}}}
 if($report){$report.ended_utc=[DateTime]::UtcNow.ToString('O');Save-LabReport}
 if($controller){$controller.running=$false;$controller.ended_utc=[DateTime]::UtcNow.ToString('O');$controller|ConvertTo-Json|Set-Content -LiteralPath $controllerPath -Encoding utf8NoBOM}
 $env:TEMP=$saved.TEMP;$env:TMP=$saved.TMP;$env:HIBERNATION_SESSION_KEY=$saved.HIBERNATION_SESSION_KEY
 $lockStream.Dispose();Remove-Item -LiteralPath $runLock
}
Write-Output ('Continuing personality archive: '+$LabRoot+"`nEpisode report: "+$reportPath)
