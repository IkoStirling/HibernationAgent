param(
 [string]$ProjectRoot=(Split-Path $PSScriptRoot -Parent),
 [string]$BuildRoot='',
 [Parameter(Mandatory)][string]$LabRoot,
 [Parameter(Mandatory)][string]$Episode,
 [string]$SourceConfig='',
 [string]$ProfileId='',
 [string]$NodeExecutable=$env:HA_NODE_RUNTIME,
 [string]$BudgetLedger='',
 [string]$DeadlineUtc='2026-10-28T00:37:47Z',
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
if($steps.version-ne1-or!$steps.title-or$steps.turns.Count-lt1-or$steps.turns.Count-gt20){throw 'Episode requires version 1, title and 1..20 meaningful turns'}
if($steps.opportunitySeconds-and(!$steps.autonomy-or$steps.opportunitySeconds-lt1-or$steps.opportunitySeconds-gt45)){throw 'An independent activity opportunity requires autonomy and 1..45 seconds'}
foreach($step in $steps.turns){if(!$step.text-or$step.text.Length-gt3000){throw 'Episode text must be 1..3000 characters'};if($step.settleSeconds-and($step.settleSeconds-lt0-or$step.settleSeconds-gt30)){throw 'Activity settlement is limited to 30 seconds'}}
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
$service=$null;$desktop=$null;$relay=$null;$relayOrigin='';$relayCapability='';$copies=@()
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
  foreach($pluginId in @('widget','atelier','habitat','pocket','reading')){Copy-Item -LiteralPath (Join-Path $BuildRoot ('plugins/'+$pluginId)) -Destination (Join-Path $runtime plugins) -Recurse -Force}
 }
 $character=Join-Path $LabRoot character
 if(!(Test-Path -LiteralPath $character)){Copy-Item -LiteralPath (Join-Path $ProjectRoot character) -Destination $character -Recurse}
 $cfg.dataRoot=Join-Path $LabRoot data;$cfg.draftsRoot=Join-Path $cfg.dataRoot drafts;$cfg.materialsRoot=Join-Path $cfg.dataRoot materials
 $cfg.characterRoot=$character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $runtime plugins;$cfg.port=0
 $cfg.frontend.startupAnimation=$false;$cfg.computer=@{enabled=$false;allowScreenCapture=$false;readRoots=@()}
 $cfg.plugins=@{
  widget=@{enabled=$true;skin='asterlan';size=168;animationMode='sequence';autonomousMode=$false;roam=$false;showBubble=$false;respectSystemMotion=$false;reactionCacheEnabled=$false}
  atelier=@{enabled=$true;maximumPieces=48}
  habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;maxObjects=6;lifetimeMinSeconds=600;lifetimeMaxSeconds=1200}
  pocket=@{enabled=$true;scrambleSteps=3;maximumMoves=24}
  reading=@{enabled=$false;language='zh';maxRequestsPerHour=12;useSystemProxy=$true}
 }
 $cfg.maxStartsPerHour=600;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false
 if($steps.autonomy){$cfg.plugins.widget.autonomousMode=$true;$cfg.plugins.widget.roam=$true;$cfg.plugins.widget.autonomyIdleSeconds=600;$cfg.plugins.widget.autonomyRefillSeconds=600}
 if($steps.opportunitySeconds){$cfg.plugins.widget.autonomyIdleSeconds=15;$cfg.plugins.widget.autonomyRefillSeconds=60}
 $cfg.maxToolRounds=8;$cfg.skills=@{enabled=$true;useBuiltins=$true;root='';grants=@()}
 $secretRoot=Join-Path $cfg.dataRoot runtime/secrets;New-Item -ItemType Directory -Force $secretRoot|Out-Null
 $runRoot=Join-Path $LabRoot ('episodes/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Force $runRoot|Out-Null
 $reportPath=Join-Path $runRoot report.json
 $report=@{version=1;title=$steps.title;started_utc=[DateTime]::UtcNow.ToString('O');live=[bool]$Live;production_archive_read=$false;continuing_archive=$true;binary_sha256=(Get-FileHash -LiteralPath (Join-Path $runtime hibernation-agent.exe) -Algorithm SHA256).Hash;persona_sha256=(Get-FileHash -LiteralPath (Join-Path $character persona.json) -Algorithm SHA256).Hash;turns=@()}
 if($Live){
  if([DateTime]::UtcNow-ge[DateTime]::Parse($DeadlineUtc).ToUniversalTime()){throw 'Authorized work window has ended'}
  $relayCapability=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24)).ToLowerInvariant();$routes=@{}
  foreach($profile in $cfg.profiles){
   if($profile.protocol-eq'mock'){continue}
   $routes[$profile.id]=@{origin=$profile.origin;basePath=$profile.basePath}
   $profile.useSystemProxy=$false;$profile.proxyOrigin=''
   $protectedFile=Join-Path $source.dataRoot ('runtime/secrets/'+$profile.id+'.dpapi')
   if(Test-Path -LiteralPath $protectedFile){$destination=Join-Path $secretRoot ($profile.id+'.dpapi');Copy-Item -LiteralPath $protectedFile -Destination $destination;$copies+=$destination}
  }
  $relayConfig=Join-Path $runRoot relay.json;$relayReady=Join-Path $runRoot relay-ready.json
  @{ledger=$BudgetLedger;maxCalls=4000;maxTokens=22000000;routes=$routes;capability=$relayCapability;deadline=$DeadlineUtc}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $relayConfig -Encoding utf8NoBOM
  $relay=Start-Process -FilePath $NodeExecutable -ArgumentList @(('"'+(Join-Path $ProjectRoot tools/PersonalityBudget.mjs)+'"'),('"'+$relayConfig+'"'),('"'+$relayReady+'"')) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $runRoot relay-error.log)
  $end=[DateTime]::UtcNow.AddSeconds(12);while(!(Test-Path -LiteralPath $relayReady)){if($relay.HasExited-or[DateTime]::UtcNow-ge$end){throw 'Budget relay did not start'};Start-Sleep -Milliseconds 100}
  $relayOrigin='http://127.0.0.1:'+((Get-Content -LiteralPath $relayReady -Raw|ConvertFrom-Json).port)
  foreach($profile in $cfg.profiles){if($profile.protocol-ne'mock'){$profile.origin=$relayOrigin;$profile.basePath='/'+$relayCapability+'/'+$profile.id}}
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
 $null=Api '/api/model' @{id=$ProfileId}
 foreach($step in $steps.turns){
  $requestId=[Guid]::NewGuid().ToString('N');$before=Api '/api/state';$watch=[Diagnostics.Stopwatch]::StartNew()
  $null=Api '/api/message' @{request_id=$requestId;text=$step.text};AwaitIdle
  if($step.settleSeconds){$end=[DateTime]::UtcNow.AddSeconds($step.settleSeconds);do{$activity=Api '/api/activities/status';if($activity.queued-eq0-and$null-eq$activity.active){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end)}
  $chat=Api '/api/chat';$state=Api '/api/state';$job=@($state.jobs|Where-Object id -eq chat)[0]
  $trace=$null;if($job.result){$jobResult=$job.result|ConvertFrom-Json;if($jobResult.trace_id){$trace=Api '/api/trace' @{id=$jobResult.trace_id}}}
  $reply=$null;$last=@($chat.messages|Where-Object role -eq assistant|Select-Object -Last 1)
  if($last.Count){$record=Api '/api/archive' @{id=$last[0].id};if($record.records[0].data.request_id-eq$requestId){$reply=$last[0].text}}
  $row=@{input=$step.text;reply=$reply;failed=$chat.call_failed;call_error=$chat.call_error;elapsed_ms=$watch.ElapsedMilliseconds;state_before=$before.continuous_state;state_after=$state.continuous_state;activities=(Api '/api/activities/status');trace=$trace}
  $report.turns+=,$row;$report.updated_utc=[DateTime]::UtcNow.ToString('O')
  $report|ConvertTo-Json -Depth 85|Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM
  Write-Output ($step.text+"`n小爱："+$reply)
  if($chat.call_failed){throw 'Episode paused after failed turn; inspect its trace before continuing the same archive'}
 }
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
  $report|ConvertTo-Json -Depth 85|Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM
  Write-Output ('Independent activity: choice='+$choice+', outcomes='+$new.Count+', error='+$activity.last_error)
 }
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(12000)}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($desktop){$desktop.Dispose()}
 foreach($copy in $copies){Remove-Item -LiteralPath $copy -ErrorAction SilentlyContinue}
 if($relay){$relay.Refresh();if(!$relay.HasExited){try{$null=Invoke-RestMethod -Method Post -Uri ($relayOrigin+'/'+$relayCapability+'/stop') -TimeoutSec 10;$null=$relay.WaitForExit(5000)}catch{};$relay.Refresh();if(!$relay.HasExited){Stop-Process -Id $relay.Id}}}
 if($report){$report.ended_utc=[DateTime]::UtcNow.ToString('O');$report|ConvertTo-Json -Depth 85|Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM}
 $env:TEMP=$saved.TEMP;$env:TMP=$saved.TMP;$env:HIBERNATION_SESSION_KEY=$saved.HIBERNATION_SESSION_KEY
 $lockStream.Dispose();Remove-Item -LiteralPath $runLock
}
Write-Output ('Continuing personality archive: '+$LabRoot+"`nEpisode report: "+$reportPath)
