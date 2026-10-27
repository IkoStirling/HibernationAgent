param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$checks=0
$root=Join-Path $ScratchRoot ('pocket-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='pocket-fixture-only'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer pocket-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 60 -Compress))};try{Invoke-RestMethod @p}catch{throw ("API "+$Path+" failed: "+$_.Exception.Message)}}
function Plugin($Name,$Arguments=@{}){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Await {(Api '/api/plugin/result').state-in@('succeeded','failed')} $Name;(Api '/api/plugin/result').result}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Backend exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'pocket clean shutdown'}
function Game{(Plugin pocket.observe).result.game}
try{
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0]);$cfg.plugins=@{pocket=@{enabled=$true;scrambleSteps=4;maximumMoves=24}};$cfg.computer=@{enabled=$false};$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content -LiteralPath $configPath -Encoding utf8;Start-Backend
 Check ($null-eq(Game)) 'pocket starts without invented prior games'
 $result=Plugin pocket.start;$game=$result.result.game;Check ($result.ok-and!$result.result.completed-and$game.status-eq'active') 'new puzzle is real and not reported as completed'
 Check (@((Api '/api/state').jobs|Where-Object result_truncated -eq $true).Count-ge1) 'long Chinese tool receipts keep a valid UTF8 diagnostic summary'
 $before=$game|ConvertTo-Json -Depth 15 -Compress
 $bad=Plugin pocket.press @{game_id=$game.id;expected_revision=1;cell=0;board=0};Check (!$bad.ok-and$bad.error.code-eq'invalid_tool_arguments'-and(Game|ConvertTo-Json -Depth 15 -Compress)-eq$before) 'model cannot assign the board or win state'
 $bad=Plugin pocket.press @{game_id=$game.id;expected_revision=2;cell=0};Check (!$bad.ok-and$bad.error.code-eq'pocket_revision_conflict') 'stale revision cannot change state'
 $board=0;for($y=0;$y-lt3;$y++){for($x=0;$x-lt3;$x++){if($game.rows[$y][$x]){$board=$board-bor(1-shl($y*3+$x))}}}
 $masks=@();for($cell=0;$cell-lt9;$cell++){$mask=0;for($other=0;$other-lt9;$other++){if([Math]::Abs([int][Math]::Floor($cell/3)-[int][Math]::Floor($other/3))+[Math]::Abs(($cell%3)-($other%3))-le1){$mask=$mask-bor(1-shl$other)}};$masks+=,$mask}
 $solution=$null;for($choice=0;$choice-lt512;$choice++){$final=$board;for($cell=0;$cell-lt9;$cell++){if($choice-band(1-shl$cell)){$final=$final-bxor$masks[$cell]}};if(!$final){$solution=$choice;break}}
 Check ($null-ne$solution) 'program-generated puzzle has a solution under independent rule simulation'
 for($cell=0;$cell-lt9;$cell++){if($solution-band(1-shl$cell)){$result=Plugin pocket.press @{game_id=$game.id;expected_revision=$game.revision;cell=$cell};Check $result.ok 'legal press applies once';$game=$result.result.game}}
 Check ($game.status-eq'won'-and$game.lit_count-eq0-and$result.result.completed) 'only actual solved board reports completion'
 $null=Api '/api/message' @{request_id='pocket-completion-followup';text='刚才的小玩具怎么样了？'};Await {!(Api '/api/chat').busy} 'followup'
 $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$trace=Api '/api/trace' @{id=($job.result|ConvertFrom-Json).trace_id};$found=$false
 foreach($id in $trace.selected_ids){$record=Api '/api/archive' @{id=$id};foreach($row in $record.records){if($row.data.observation.kind-eq'pocket.solved'){$found=$true}}}
 Check $found 'later conversation receives a real solved experience'
 $result=Plugin pocket.start;$game=$result.result.game;$result=Plugin pocket.press @{game_id=$game.id;expected_revision=$game.revision;cell=0};$game=$result.result.game
 if($game.status-ne'active'){$result=Plugin pocket.start;$game=$result.result.game}
 $before=$game|ConvertTo-Json -Depth 15 -Compress;Stop-Backend;Start-Backend
 Check ((Game|ConvertTo-Json -Depth 15 -Compress)-eq$before) 'unfinished puzzle and exact revision survive restart'
 $null=Api '/api/control' @{paused=$true}
 $bad=Invoke-WebRequest -Uri ($script:origin+'/api/plugin/test') -Headers @{Authorization='Bearer pocket-fixture-only'} -Method Post -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes((@{tool='pocket.press';args=@{game_id=$game.id;expected_revision=$game.revision;cell=1}}|ConvertTo-Json -Depth 15 -Compress))) -SkipHttpErrorCheck
 $pausedGame=@((Api '/api/capabilities').plugins|Where-Object id -eq pocket)[0].current.current.game
 Check ($bad.StatusCode-eq409-and($pausedGame|ConvertTo-Json -Depth 15 -Compress)-eq$before) 'pause blocks mutations without resetting progress';$null=Api '/api/control' @{paused=$false}
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 60|ConvertFrom-Json -AsHashtable;$settings.plugins.pocket.enabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Check (@((Api '/api/capabilities').tools|Where-Object {$_.tool-like'pocket.*'}).Count-eq0) 'disabled puzzle has no callable tool'
 $settings.plugins.pocket.enabled=$true;$null=Api '/api/settings' @{settings=$settings};Check ((Game|ConvertTo-Json -Depth 15 -Compress)-eq$before) 'reenabling restores progress instead of inventing a new board'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Pocket passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
