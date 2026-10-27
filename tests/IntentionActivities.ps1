param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/intention-activities-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=Join-Path $root tmp;$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null;$env:HIBERNATION_SESSION_KEY='intention-fixture-only'
$service=$null;$desktop=$null;$provider=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer intention-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 60 -Compress))};Invoke-RestMethod @p}
function Tool($Id,$Name,$Arguments){@{id=$Id;tool=$Name;args=$Arguments}}
function Calls($Items){@{version=1;kind='call';text=$null;calls=@($Items)}}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Plan($Responses){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function Message($Responses){Plan $Responses;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='合成相处：我想看看那件没做完的小事。'};Await {!(Api '/api/chat').busy} 'turn';Check (!(Api '/api/chat').call_failed) 'turn completes';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function Observe{
 $null=Message @((Calls @((Tool self self.observe @{}))),(Silent))
 $capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 70
 ($capture.messages[-1].content|ConvertFrom-Json -Depth 60).tool_results[0].result
}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false
 $cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';size=168;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;roam=$false;respectSystemMotion=$false;showBubble=$false;reactionCacheEnabled=$false}}
 $cfg.plugins.atelier=@{enabled=$true;maximumPieces=12}
 $cfg.plugins.pocket=@{enabled=$true;scrambleSteps=3;maximumMoves=24}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Intent fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content -LiteralPath $configPath -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$service=$desktop.Child
 Await {if($service.HasExited){throw 'Backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='actor'}
 $wish=@{id='leave-it-a-moment';expected_revision=0;kind='intention';topic='留一点时间再看纸船';note='INTENTION_PRIVATE_MARKER 想让它先在角落待一会儿';stance='want';source_ids=@('${event_id}')}
 $command=@{kind='wait';seconds=1;purpose='先留一小会儿';intention=@{id=$wish.id;revision=1}}
 $null=Message @((Calls @((Tool wish self.notice $wish))),(Calls @((Tool queue activity.enqueue @{commands=@($command)}))),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.status-eq'completed'-and$_.intention.id-eq$wish.id}).Count-eq1} 'bound wait actually completes'
 $view=Observe;$thread=@($view.threads|Where-Object id -eq $wish.id)[0]
 Check ($thread.status-eq'active'-and$thread.attempts.Count-eq1-and$thread.attempts[0].status-eq'completed'-and$thread.attempts[0].started) 'actual action outcome persists beside an intention without auto-completing the larger wish'
 $source=$thread.attempts[0].source_id;Check ($source-and$thread.attempts[0].source_revision-eq1) 'attempt has an immutable private provenance identity'
 $close=@{id=$wish.id;expected_revision=1;outcome='completed';note='整个打算已经完成';source_ids=@($source)}
 $trace=Message @((Calls @((Tool close self.close $close))),(Silent));Check ($trace.tool_results[0].error_code-eq'personal_source_not_available') 'a private progress record is not standalone proof of completing a larger intention'
 $command.seconds=60
 $null=Message @((Calls @((Tool queue activity.enqueue @{commands=@($command,$command)}))),(Silent));Await {(Api '/api/activities/status').active} 'bound wait starts'
 $null=Message @((Silent));$view=Observe;$thread=@($view.threads|Where-Object id -eq $wish.id)[0]
 Check ($thread.status-eq'active'-and@($thread.attempts|Where-Object status -eq new_input).Count-eq1-and@($thread.attempts|Where-Object status -eq not_started_new_input).Count-eq1) 'new input records both interrupted work and never-started plans while keeping the wish open'
 $updated=$wish.Clone();$updated.expected_revision=1;$updated.note='改为看看它最终停在哪里'
 $command.seconds=1
 $null=Message @((Calls @((Tool queue activity.enqueue @{commands=@($command)}))),(Calls @((Tool refine self.notice $updated))),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object status -eq not_started_intention_changed).Count-eq1} 'changed intention cancels its stale queued step'
 $view=Observe;$thread=@($view.threads|Where-Object id -eq $wish.id)[0]
 Check ($thread.revision-eq2-and@($thread.attempts|Where-Object {$_.status-eq'not_started_intention_changed'-and!$_.started}).Count-eq1) 'semantic revision change cannot silently execute an earlier plan'
 $close.expected_revision=2;$close.outcome='released';$close.note='现在先不追着它看了';$close.source_ids=@('${event_id}')
 $null=Message @((Calls @((Tool release self.close $close))),(Silent))
 $command.intention.revision=2
 $trace=Message @((Calls @((Tool stale activity.enqueue @{commands=@(@{kind='wait';seconds=1},$command)}))),(Silent))
 Check ($trace.tool_results[0].error_code-eq'activity_intention_changed_or_unavailable'-and(Api '/api/activities/status').queued-eq0) 'released intention rejects an entire new batch without partial execution'
 $writer=$wish.Clone();$writer.id='write-a-corner';$writer.topic='留下一个角落';$writer.note='想写下纸船的一个小变化'
 $creative=@{kind='creative_note';seconds=0;title='角落';content='纸船绕过了那一点水渍，停在玻璃边。';piece_kind='note';intention=@{id=$writer.id;revision=1}}
 $null=Message @((Calls @((Tool notice self.notice $writer))),(Calls @((Tool create activity.enqueue @{commands=@($creative)}))),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.intention.id-eq$writer.id-and$_.status-eq'completed'}).Count-eq1} 'authored activity actually writes a piece'
 $view=Observe;$thread=@($view.threads|Where-Object id -eq $writer.id)[0]
 Check ($thread.status-eq'active'-and$thread.attempts[0].result_sources.Count-eq1) 'real creation receipt is linked without automatically closing its intention'
 $finish=@{id=$writer.id;expected_revision=1;outcome='completed';note='那一点变化已经实际留在草稿里了';source_ids=@($thread.attempts[0].result_sources[0].id)}
 $trace=Message @((Calls @((Tool finish self.close $finish))),(Silent));Check ($trace.tool_results[0].ok) 'character may resolve a matching creative intention using its actual original tool receipt'
 $trace=Message @((Calls @((Tool puzzle pocket.start @{}))),(Silent));$puzzleSource=$trace.tool_results[0].source_id
 $archived=Api '/api/archive' @{id=$puzzleSource};Check ($archived.records[0].data.kind-eq'tool_observation'-and$archived.records[0].data.result.applied-and!$archived.records[0].data.result.completed) 'starting an unfinished game is an actual operation rather than a pending proposal'
 $trace=Message @((Calls @((Tool remember memory.remember @{kind='outer';text='我已经开始了一局，还没有解开。';source_ids=@($puzzleSource)}))),(Silent));Check ($trace.tool_results[0].ok) 'actual unfinished play can be remembered without falsely claiming the puzzle was solved'
 $game=@((Api '/api/capabilities').plugins|Where-Object id -eq pocket)[0].current.current.game
 $local=$wish.Clone();$local.id='see-one-change';$local.topic='看一次棋盘变化';$local.note='只想实际按一下，看格局怎样变化'
 $corner=@(@($true,$true,$false),@($true,$false,$false),@($false,$false,$false))|ConvertTo-Json -Compress;$cell=if(($game.rows|ConvertTo-Json -Compress)-eq$corner){1}else{0}
 $press=@{game_id=$game.id;expected_revision=$game.revision;cell=$cell};$finishLocal=@{id=$local.id;expected_revision=1;outcome='completed';note='已经实际按过一次，也看到了这次的变化';source_ids=@('${last_source_id}')}
 $trace=Message @((Calls @((Tool local self.notice $local))),(Calls @((Tool step pocket.press $press))),(Calls @((Tool local-done self.close $finishLocal))),(Silent))
 Check ($trace.tool_results[2].ok) 'an actual unsolved puzzle step can complete the smaller intention of trying one change'
 $game=@((Api '/api/capabilities').plugins|Where-Object id -eq pocket)[0].current.current.game
 Check ($game.status-eq'active') 'completing a local intention does not change the actual puzzle to won'
 $beforeRows=$game.rows|ConvertTo-Json -Compress;$leave=@{kind='puzzle_leave';game_id=$game.id;expected_revision=$game.revision;purpose='暂时不解了，去看看别的'}
 $trace=Message @((Calls @((Tool leave activity.enqueue @{commands=@($leave)}))),(Silent));Check ($trace.tool_results[0].ok) 'voluntarily leaving an active game can be scheduled as a finite real action'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'puzzle_leave'-and$_.status-eq'completed'}).Count-eq1} 'voluntary leave completes'
 $after=@((Api '/api/capabilities').plugins|Where-Object id -eq pocket)[0].current.current.game
 Check ($after.id-eq$game.id-and$after.status-eq'left'-and$after.revision-eq$game.revision+1-and($after.rows|ConvertTo-Json -Compress)-eq$beforeRows) 'leaving preserves actual unsolved board rather than marking it won'
 $result=@((Api '/api/activities/status').recent|Where-Object kind -eq puzzle_leave)[0];$source=Api '/api/archive' @{id=$result.source_id}
 Check ($source.records[0].data.kind-eq'tool_observation'-and$source.records[0].data.result.applied-and!$source.records[0].data.result.completed) 'voluntary leave has an original actual-operation receipt while the puzzle remains unfinished'
 $private=$wish.Clone();$private.id='private-purpose';$private.note='这是属于一个来源的私人打算'
 $privateCommand=@{kind='wait';seconds=1;purpose='ACTIVITY_PRIVATE_PURPOSE';intention=@{id=$private.id;revision=1}}
 $null=Message @((Calls @((Tool private self.notice $private))),(Calls @((Tool remember memory.remember @{kind='inference';text='活动目的权限的合成来源';source_ids=@('${event_id}')}))),(Calls @((Tool private-queue activity.enqueue @{commands=@($privateCommand)}))),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.intention.id-eq$private.id-and$_.status-eq'completed'}).Count-eq1} 'private-purpose action completes'
 $memory=@((Api '/api/memories').memories|Where-Object text -eq '活动目的权限的合成来源')[0].id;Check ([bool]$memory) 'private activity has a real owner-controlled source'
 $null=Message @((Calls @((Tool status activity.status @{}))),(Silent));$visible=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw
 Check ($visible.Contains('ACTIVITY_PRIVATE_PURPOSE')-and!$visible.Contains('narrative_sources')) 'permitted purpose reaches the actor without granting its private provenance as tool authority'
 $null=Api '/api/access' @{id=$memory;accessible=$false}
 $trace=Message @((Calls @((Tool status activity.status @{}))),(Silent));$hidden=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw
 Check (!$hidden.Contains('ACTIVITY_PRIVATE_PURPOSE')-and!$hidden.Contains('private-purpose')) 'restricted purpose and binding disappear from body, pre-input body, self and explicit model activity status'
 $raw=@((Api '/api/activities/status').recent|Where-Object {$_.intention.id-eq$private.id})[0]
 Check ($raw.purpose-eq'ACTIVITY_PRIVATE_PURPOSE'-and$raw.status-eq'completed'-and$raw.narrative_sources.Count-gt0) 'owner diagnostics preserve original purpose, lineage and actual completed result'
 Check (($trace.body_at_input.activity_queue.recent|ConvertTo-Json -Depth 30).Contains('completed')) 'restricted narrative does not erase the physical activity history'
 $null=Api '/api/access' @{id=$memory;accessible=$true}
 $null=Message @((Calls @((Tool status activity.status @{}))),(Silent));$restored=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw
 Check ($restored.Contains('ACTIVITY_PRIVATE_PURPOSE')) 'restored sources expose the same preserved purpose again without rewriting history'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown'
 Check (!([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)))).Contains('INTENTION_PRIVATE_MARKER')) 'intentions and attempts stay encrypted'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Intention activities passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
