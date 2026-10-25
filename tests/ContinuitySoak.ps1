param(
 [string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),
 [string]$BuildRoot='',
 [ValidateRange(1,720)][double]$DurationMinutes=480,
 [ValidateRange(1,120)][int]$CycleSeconds=30,
 [string]$RunRoot=''
)
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot 'out/staging'};$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
if(!$RunRoot){$RunRoot=Join-Path $ProjectRoot ('artifacts/soak-'+[Guid]::NewGuid().ToString('N'))};$RunRoot=[IO.Path]::GetFullPath($RunRoot)
if(Test-Path "$RunRoot/progress.json"){throw 'Choose a new, unused soak run directory.'}
New-Item -ItemType Directory -Force $RunRoot|Out-Null
$service=$null;$provider=$null;$checks=0;$cycle=0;$restarts=0;$busyRetries=0;$lastMemory='';$lastMessage='';$started=[DateTime]::UtcNow;$end=$started.AddMinutes($DurationMinutes)
$env:HIBERNATION_SESSION_KEY='isolated-soak-session';$env:HIBERNATION_SOAK_KEY='isolated-soak-model';$env:TEMP="$RunRoot/tmp";$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=30){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if(& $Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){
 $a=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer isolated-soak-session'};TimeoutSec=12;SkipHttpErrorCheck=$true};if($null-ne$Data){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))}
 for($attempt=0;$attempt-lt60;$attempt++){$response=Invoke-WebRequest @a;$value=$response.Content|ConvertFrom-Json -Depth 60;if($response.StatusCode-ge200-and$response.StatusCode-lt300){return $value};if($response.StatusCode-eq409-and$value.error-eq'busy'-and$Path-in@('/api/message','/api/settings','/api/model')){$script:busyRetries++;Start-Sleep -Milliseconds 100;continue};throw "Request failed: $Path HTTP $($response.StatusCode) $($value.error)"};throw "Busy retry deadline: $Path"
}
function Plan($Responses){$text=@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses)}|ConvertTo-Json -Depth 40 -Compress;[IO.File]::WriteAllText("$RunRoot/provider/plan.next",$text,[Text.UTF8Encoding]::new($false));[IO.File]::Move("$RunRoot/provider/plan.next","$RunRoot/provider/plan.json",$true)}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@()}}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Message($Text){$id=[Guid]::NewGuid().ToString('N');$null=Api '/api/message' @{request_id=$id;text=$Text};Wait-For {!(Api '/api/chat').busy} 'model turn';$chat=Api '/api/chat';Check (!$chat.call_failed) 'provider turn completes';$script:lastMessage=$chat.messages[-1].id;return $id}
function Feedback{$capture=Get-Content "$RunRoot/provider/last-request.json" -Raw|ConvertFrom-Json -Depth 60;($capture.messages[-1].content|ConvertFrom-Json -Depth 50).tool_results}
function Start-Backend{
 Remove-Item "$RunRoot/ready.json" -ErrorAction SilentlyContinue
 $script:service=Start-Process "$BuildRoot/hibernation-agent.exe" -ArgumentList ('--config "'+$RunRoot+'/config.json" --ready-file "'+$RunRoot+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$RunRoot/service.log" -RedirectStandardError "$RunRoot/error.log"
 Wait-For {if($service.HasExited){throw 'isolated backend exited'};Test-Path "$RunRoot/ready.json"} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content "$RunRoot/ready.json" -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(12000)-and$service.ExitCode-eq0) 'clean shutdown'}
function Progress($Status,$Failure=''){
 $process=if($service-and!$service.HasExited){$service.Refresh();$service}else{$null}
 $value=@{status=$Status;startedUtc=$started.ToString('O');plannedEndUtc=$end.ToString('O');updatedUtc=[DateTime]::UtcNow.ToString('O');elapsedMinutes=([DateTime]::UtcNow-$started).TotalMinutes;cycles=$cycle;checks=$checks;restarts=$restarts;busyRetries=$busyRetries;error=$Failure;root=$RunRoot;servicePid=if($process){$process.Id}else{$null};providerPid=if($provider-and!$provider.HasExited){$provider.Id}else{$null};privateMiB=if($process){[math]::Round($process.PrivateMemorySize64/1MB,2)}else{0};handles=if($process){$process.HandleCount}else{0}}
 $text=$value|ConvertTo-Json -Depth 5;[IO.File]::WriteAllText("$RunRoot/progress.next",$text,[Text.UTF8Encoding]::new($false));[IO.File]::Move("$RunRoot/progress.next","$RunRoot/progress.json",$true)
 ($value|ConvertTo-Json -Depth 5 -Compress)|Add-Content "$RunRoot/samples.jsonl" -Encoding utf8
}
try{
 $provider=Start-Process "$BuildRoot/Hibernation_TestProvider.exe" -ArgumentList ('"'+$RunRoot+'/provider" 43200') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$RunRoot/provider.log" -RedirectStandardError "$RunRoot/provider-error.log"
 Wait-For {Test-Path "$RunRoot/provider/port"} 'synthetic provider';$fixture='http://127.0.0.1:'+(Get-Content "$RunRoot/provider/port")
 $cfg=Get-Content "$ProjectRoot/config/example.json" -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot="$RunRoot/data";$cfg.characterRoot="$ProjectRoot/character";$cfg.uiRoot="$ProjectRoot/ui";$cfg.pluginRoot="$BuildRoot/plugins";$cfg.maxStartsPerHour=1000;$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000;$cfg.plugins=@{widget=@{enabled=$true;roam=$false;reactionCacheEnabled=$false;skin='dinosaur';palette='forest'};weather=@{enabled=$true;city='合成测试城市';refreshMinutes=30;geocodingOrigin=$fixture;forecastOrigin=$fixture}}
 $cfg.profiles+=@{id='fixture';label='Soak fixture';enabled=$true;protocol='chat-completions';origin=$fixture;basePath='/v1';model='fixture-model';credentialEnvironment='HIBERNATION_SOAK_KEY';timeoutMs=3000;maxOutputTokens=2048};$cfg|ConvertTo-Json -Depth 40|Set-Content "$RunRoot/config.json" -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='fixture'};Progress 'running'
 while([DateTime]::UtcNow-lt$end){
  if(Test-Path "$RunRoot/stop"){Progress 'stopped';break}
  $cycleStart=[DateTime]::UtcNow;$cycle++;$label="合成长期检查-$cycle-"+[Guid]::NewGuid().ToString('N');$activity=@('idle','reflecting','resting')[$cycle%3]
  Plan @(@{response=(Calls @((Tool 'remember' 'memory.remember' @{kind='conversation';text=$label;source_ids=@('${event_id}')}),(Tool 'state' 'state.propose' @{activity=$activity;intensity=0.1;valence=0.1;source_id='${event_id}';focus='合成测试环境'}),(Tool 'draft' 'workspace.write' @{path='soak/current.txt';content=$label})))},@{response=(Reply '这一轮保持了连续记录。')})
  $null=Message "合成输入 $cycle";$feedback=Feedback;Check ($feedback.Count-eq3-and@($feedback|Where-Object {!$_.ok}).Count-eq0) 'memory state workspace receipts';$lastMemory=$feedback[0].result.id
  $read=Api '/api/workspace' @{mount='drafts';path='soak/current.txt';read=$true};Check ($read.content-eq$label) 'authorized draft matches successful receipt'
  Plan @(@{response=(Calls @((Tool 'read' 'memory.read' @{id=$lastMemory})))},@{response=(Reply '这条经历有来源。')});$null=Message "合成来源核对 $cycle";$readMemory=Feedback;Check ($readMemory[0].ok-and$readMemory[0].result.memories[0].text-eq$label-and$readMemory[0].result.memories[0].sources.Count-gt0) 'memory retains exact content and provenance'
  if($cycle%4-eq0){Plan @(@{response=(Calls @((Tool 'escape' 'workspace.write' @{path='../archive/forbidden';content='should never exist'})))},@{response=(Reply '这次操作没有获准。')});$null=Message '合成越界操作检查';Check (!(Feedback)[0].ok-and!(Test-Path "$RunRoot/data/archive/forbidden")) 'workspace boundary remains enforced'}
  if($cycle%5-eq0){Plan @(@{response='invalid-json'},@{response=(Reply '格式恢复后才显示。')});$null=Message '合成格式恢复检查';Check ((Api '/api/chat').messages[-1].text-eq'格式恢复后才显示。') 'malformed output is repaired before display'}
  if($cycle%6-eq0){$null=Api '/api/control' @{paused=$true};$before=(Api '/api/state').continuous_state;Check ($null-ne$before.energy-and$null-ne$before.mood) 'actual continuous numbers are present';Start-Sleep -Milliseconds 300;$after=(Api '/api/state').continuous_state;Check ($before.mood-eq$after.mood-and$before.energy-eq$after.energy) 'pause preserves numerical state';$null=Api '/api/control' @{paused=$false}}
  if($cycle%8-eq0){$settings=(Api '/api/dashboard').settings;$settings.plugins.widget.skin=@('dinosaur','moonbird','lumen')[($cycle/8)%3];$null=Api '/api/settings' @{settings=$settings};Check ((Api '/api/capabilities').plugins.Where({$_.id-eq'widget'})[0].current.skin-eq$settings.plugins.widget.skin) 'plugin hot configuration remains usable'}
  if($cycle%12-eq0){$before=(Api '/api/state').continuous_state;Check ($null-ne$before.energy-and$null-ne$before.mood) 'restart reads actual continuous numbers';$raw=Api '/api/archive' @{id=$lastMessage};Stop-Backend;Start-Backend;$restarts++;$after=(Api '/api/state').continuous_state;Check ([math]::Abs($after.mood-$before.mood)-lt.02-and[math]::Abs($after.energy-$before.energy)-lt.02) 'restart preserves continuous state';$restored=Api '/api/archive' @{id=$lastMessage};Check (($raw|ConvertTo-Json -Depth 30 -Compress)-eq($restored|ConvertTo-Json -Depth 30 -Compress)) 'original archive stays unchanged across restart'}
  $health=Api '/api/chat';Check ($health.health-eq'ok'-and!$health.busy) 'owner loop healthy';$service.Refresh();Check ($service.PrivateMemorySize64-lt512MB-and$service.HandleCount-lt2048) 'bounded process resources';Progress 'running'
  while([DateTime]::UtcNow-lt$cycleStart.AddSeconds($CycleSeconds)-and[DateTime]::UtcNow-lt$end){if(Test-Path "$RunRoot/stop"){break};Start-Sleep -Milliseconds 1000}
 }
 if([DateTime]::UtcNow-ge$end){Check (([DateTime]::UtcNow-$started).TotalMinutes-ge$DurationMinutes) 'real duration reached';Progress 'completed'}
}catch{Progress 'failed' $_.Exception.Message;throw}
finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{Stop-Backend}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
 if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}
}
