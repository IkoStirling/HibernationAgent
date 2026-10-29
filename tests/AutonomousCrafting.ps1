param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
. (Join-Path $PSScriptRoot 'ProjectedRequest.ps1')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release}
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/autonomous-crafting-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='crafting-fixture-only';$service=$null;$provider=$null;$desktop=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=25){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 40};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer crafting-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 50|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function Silent{@{version=1;kind='silence';text=$null;calls=@()}}
function Calls($Commands){@{version=1;kind='call';text=$null;calls=@(@{id='activities';tool='activity.enqueue';args=@{commands=@($Commands)}})}}
function Message($Responses,[string]$Text='独立桌面活动验证'){Plan $Responses;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'turn';Check (!(Api '/api/chat').call_failed) 'turn completes'}
function Feedback{$capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;($capture.messages[-1].content|ConvertFrom-Json).tool_results[0]}
function Snapshot($Id){(Api '/api/capabilities').plugins.Where({$_.id-eq$Id})[0].current}
function Body{Message @(@{version=1;kind='call';text=$null;calls=@(@{id='body';tool='body.observe';args=@{}})},(Silent));(Feedback).result}
function Plugin($Name,$Arguments=@{}){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Await {(Api '/api/plugin/result').state-in@('succeeded','failed')} $Name;(Api '/api/plugin/result').result}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 @{pages=@(@{id=1;key='Blue_moss';title='蓝苔';excerpt='雨后的蓝光'})}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root provider/reading-search.json) -Encoding utf8
 @{title='蓝苔';lang='zh';extract='虚构的蓝苔在雨后发出淡淡的蓝色光。';pageid=1;revision='crafting-fixture';type='standard'}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root provider/reading-summary.json) -Encoding utf8
 @{title='蓝苔';content_model='wikitext';source='== 夜里 == 蓝苔的光靠近水边时微弱。';id=1;latest=@{id=43}}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $root provider/reading-source.json) -Encoding utf8
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins
 $cfg.frontend.startupAnimation=$false;$cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Crafting fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';size=168;autonomousMode=$true;autonomyIdleSeconds=600;autonomyRefillSeconds=600;roam=$false;showBubble=$false;respectSystemMotion=$false};habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;maxObjects=4;lifetimeMinSeconds=120;lifetimeMaxSeconds=120};atelier=@{enabled=$true;maximumPieces=12};pocket=@{enabled=$true;scrambleSteps=4;maximumMoves=24}}
 $cfg.plugins.reading=@{enabled=$true;autonomousReading=$true;language='zh';serviceOrigin=$fixture;useSystemProxy=$false}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $configPath -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$configPath,$ready,$ProjectRoot);$service=$desktop.Child
 Await {Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 Await {(Snapshot widget).animation.ready} 'body ready'
 $initialBody=Body;Check ($initialBody.available_commands-contains'creative_note'-and$initialBody.available_commands-notcontains'creative_continue'-and$initialBody.available_commands-notcontains'puzzle_press'-and$initialBody.available_commands-notcontains'reference_read') 'empty world offers new work and search without inventing old drafts games or article keys'
 Check ($initialBody.available_commands-notcontains'creative_read'-and$initialBody.available_commands-notcontains'interact'-and$initialBody.available_commands-notcontains'arrange') 'empty world does not offer a nonexistent piece or explicit physical object target'
 $drop=@{kind='create_drop';seconds=0;template='paper_star';name='慢半拍';description='折痕不太整齐的纸星';tint='amber'}
 $note=@{kind='creative_note';seconds=0;purpose='想给那颗纸星留一点小故事，不急着写完';title='纸星旁边';piece_kind='story';content='纸星站在角落，等一阵没有约定的风。'}
 Message @((Calls @($drop,$note,@{kind='wait';seconds=1})),(Silent))
 Check ((Feedback).ok-and(Feedback).result.accepted-eq3) 'creation and creative writing can form one deferred batch'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-in@('create_drop','creative_note')-and$_.status-eq'completed'}).Count-eq2} 'actual effects complete'
 $recent=(Api '/api/activities/status').recent;$created=@($recent|Where-Object kind -eq create_drop)[0];$written=@($recent|Where-Object kind -eq creative_note)[0]
 Check ($created.source_id-and$written.source_id) 'completed effects carry authentic observation sources'
 Check ((Snapshot habitat).objects.Count-eq1-and(Snapshot habitat).objects[0].definition.name-eq'慢半拍') 'character-created object actually appears in its desktop world'
 $observation=Api '/api/archive' @{id=$written.source_id};$pieceId=$observation.records[0].data.result.piece.id
 Check ($written.purpose-eq$note.purpose-and$observation.records[0].data.result.piece.stage-eq'draft') 'planned purpose survives execution while actual draft state remains separate'
 $piece=Plugin atelier.read @{id=$pieceId};Check ($piece.ok-and$piece.result.piece.content-eq$note.content) 'cached creative command writes actual readable content'
 $readOwn=@{kind='creative_read';seconds=0;piece_id=$pieceId;purpose='回头读自己留下的小故事'};Message @((Calls @($readOwn)),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'creative_read'-and$_.status-eq'completed'}).Count-eq1} 'cached own reading executes through the bounded read worker'
 $readReceipt=@((Api '/api/activities/status').recent|Where-Object kind -eq creative_read)[0];$readArchive=Api '/api/archive' @{id=$readReceipt.source_id}
 Check ($readArchive.records[0].data.tool-eq'atelier.read'-and$readArchive.records[0].data.result.piece.content-eq$note.content-and$readArchive.records[0].data.result.piece.revision-eq1-and(Snapshot atelier).piece_count-eq1) 'rereading returns actual unchanged work rather than treating a piece as a physical object'
 $badRead=$readOwn.Clone();$badRead.piece_id='not-a-visible-piece';Message @((Calls @($badRead)),(Silent));Check (!(Feedback).ok-and(Feedback).error.code-eq'activity_piece_unavailable') 'unknown own reading targets reject before execution'
 $continue=@{kind='creative_continue';seconds=0;piece_id=$pieceId;expected_revision=1;content='风来的时候，纸星装作没有等过。';stage='draft'}
 Message @((Calls @($continue)),(Silent));Check ((Feedback).ok) 'an observed unfinished piece can enter a continuation batch'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'creative_continue'-and$_.status-eq'completed'}).Count-eq1} 'cached continuation commits through the effect worker'
 $continued=Plugin atelier.read @{id=$pieceId};Check ($continued.result.piece.revision-eq2-and$continued.result.piece.content-eq($note.content+"`n"+$continue.content)-and(Snapshot atelier).piece_count-eq1) 'continuation preserves prior content rather than creating another unrelated note'
 Message @((Calls @($continue)),(Silent));Check (!(Feedback).ok-and(Feedback).error.code-eq'activity_piece_revision') 'known stale creative plan rejects before any append'
 $continue.expected_revision=2
 Message @((Calls @(@{kind='wait';seconds=2},$continue)),(Silent));$pieceFile=Join-Path $cfg.dataRoot ('drafts/atelier/'+$pieceId+'.json');$external=Get-Content -LiteralPath $pieceFile -Raw|ConvertFrom-Json -AsHashtable;$external.revision=3;$external.content='外部修改后不允许旧队列覆盖。';$external|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $pieceFile -Encoding utf8
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'creative_continue'-and$_.status-eq'atelier_revision_conflict'}).Count-eq1} 'execution checks the real file version again after waiting'
 Check ((Plugin atelier.read @{id=$pieceId}).result.piece.content-eq$external.content) 'external creative edit survives delayed stale append'
 Message @((Silent)) '刚才留下和续写了哪些创作，也放了什么小物？';$capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;$context=Model-CurrentEvent $capture
 Check (@($context.authorized_context|Where-Object {$_.data.observation.kind-eq'atelier.created'}).Count-ge1-and@($context.authorized_context|Where-Object {$_.data.observation.kind-eq'habitat.appeared'}).Count-ge1-and@($context.authorized_context|Where-Object {$_.data.observation.kind-eq'atelier.revised'}).Count-ge1) 'later conversation has real creation and continuation experiences instead of invented memories'
 $objectId=(Snapshot habitat).objects[0].id
 Message @((Calls @(@{kind='interact';seconds=0;object_id=$objectId;action='look'},@{kind='wait';seconds=1})),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'interact'-and$_.status-eq'completed'}).Count-eq1} 'explicit target receives real arrival and inspection' 40
 $old=(Snapshot habitat).objects[0];$destinationX=[Math]::Min(.94,[Math]::Max(.04,$old.x+.08));$destinationY=$old.y
 Message @((Calls @(@{kind='arrange';seconds=0;object_id=$objectId;x=$destinationX;y=$destinationY})),(Silent))
 Check ((Feedback).ok) 'a concrete arrangement can enter the deferred plan'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'arrange'-and$_.status-eq'completed'}).Count-eq1} 'arrangement completes only after approach and real movement'
 $moved=@((Snapshot habitat).objects|Where-Object id -eq $objectId)[0]
 Check ([Math]::Abs($moved.x-$destinationX)-lt.00001-and$moved.expires-eq$old.expires-and($moved.definition|ConvertTo-Json -Compress)-eq($old.definition|ConvertTo-Json -Compress)) 'arrangement changes position without refreshing lifetime or authored properties'
 Message @((Silent)) '刚才那个小物被挪到哪里了？';$capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;$context=Model-CurrentEvent $capture
 Check (@($context.authorized_context|Where-Object {$_.data.observation.kind-eq'habitat.arranged'}).Count-ge1) 'later dialogue receives an actual arrangement experience'
 $before=(Snapshot atelier).piece_count
 Message @((Calls @($note,@{kind='interact';seconds=0;object_id='no-such-object';action='look'})),(Silent))
 Check (!(Feedback).ok-and(Feedback).error.code-eq'activity_target_unavailable'-and(Snapshot atelier).piece_count-eq$before) 'unavailable explicit target rejects whole batch before any writing'
 Message @((Calls @(@{kind='wait';seconds=10},$note)),(Silent));Message @((Silent))
 Check ((Api '/api/activities/status').queued-eq0-and$null-eq(Api '/api/activities/status').active-and(Snapshot atelier).piece_count-eq$before) 'new input cancels a pending creative command without delayed execution'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 50|ConvertFrom-Json -AsHashtable;$settings.plugins.habitat.lifetimeMinSeconds=5;$null=Api '/api/settings' @{settings=$settings}
 $ephemeral=Plugin habitat.create @{kind='berry';lifetime_min_seconds=5;lifetime_max_seconds=5};Check $ephemeral.ok 'short-lived target created'
 Message @((Calls @(@{kind='wait';seconds=8},@{kind='interact';seconds=0;object_id=$ephemeral.result.object.id;action='look'})),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'interact'-and$_.status-eq'world_object_missing'}).Count-eq1} 'expired target yields a real failure and frees queue' 18
 Check ((Api '/api/activities/status').queued-eq0-and$null-eq(Api '/api/activities/status').active) 'expired target never leaves the body stuck waiting'
 Message @((Calls @(@{kind='puzzle_start';seconds=0})),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'puzzle_start'-and$_.status-eq'completed'}).Count-eq1} 'cached activity starts an actual private puzzle'
 $game=(Snapshot pocket).current.game;$press=@{kind='puzzle_press';seconds=0;game_id=$game.id;expected_revision=$game.revision;cell=4}
 Message @((Calls @($press)),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'puzzle_press'-and$_.status-eq'completed'}).Count-eq1} 'cached puzzle press applies through the bounded effect worker'
 Check ((Snapshot pocket).current.game.revision-eq2-and(Snapshot pocket).current.game.moves-eq1) 'cached game progress is real instead of a simulated reply'
 Message @((Calls @($press)),(Silent));Check (!(Feedback).ok-and(Feedback).error.code-eq'activity_game_revision'-and(Snapshot pocket).current.game.revision-eq2) 'known stale game plan is rejected before mutation'
 $press.expected_revision=2;Message @((Calls @(@{kind='wait';seconds=10},$press)),(Silent));Message @((Silent))
 Check ((Snapshot pocket).current.game.revision-eq2-and(Api '/api/activities/status').queued-eq0) 'new input discards a queued puzzle action without delayed replay'
 Message @((Calls @(@{kind='reference_search';seconds=0;query='蓝苔';language='zh'})),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'reference_search'-and$_.status-eq'completed'}).Count-eq1} 'cached reference search retrieves actual results through the effect worker'
 Check ((Snapshot reading).last_search.pages[0].key-eq'Blue_moss') 'next plan can see actual returned titles and keys'
 $read=@{kind='reference_read';seconds=0;key='Blue_moss';language='zh'};Message @((Calls @($read)),(Silent))
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.kind-eq'reference_read'-and$_.status-eq'completed'}).Count-eq1} 'cached reading opens an observed result instead of guessing unseen content'
 Check ((Snapshot reading).last_read.excerpt-eq'虚构的蓝苔在雨后发出淡淡的蓝色光。'-and!(Snapshot reading).last_read.full_article_read) 'actual reading excerpt becomes available for later autonomous choices'
 Message @((Silent)) '刚才读到的材料是什么内容？';$capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;$context=Model-CurrentEvent $capture
 Check (@($context.authorized_context|Where-Object {$_.data.observation.kind-eq'reading.observed'-and$_.data.observation.reference.scope-eq'lead_excerpt'}).Count-ge1) 'later conversation receives an authentic reading experience with explicit excerpt scope'
 Check (@($context.authorized_context|Where-Object {$_.data.tool-eq'reading.open'-and$_.data.context_role-eq'previous_tool_observation'-and$_.data.result.full_article_read-eq$false}).Count-eq1) 'latest autonomous tool receipt reaches later dialogue without relying on prior assistant wording'
 $sourceRead=$read.Clone();$sourceRead.detail='source';$sourceRead.offset=0;$sourceRead.length=256;Message @((Calls @($sourceRead)),(Silent))
 Await {(Snapshot reading).last_read.scope-eq'article_source_chunk'} 'cached activity passes source range arguments to the actual tool'
 Check ((Snapshot reading).last_read.excerpt-eq'== 夜里 == 蓝苔的光靠近水边时微弱。'-and(Snapshot reading).last_read.next_offset-gt0-and!(Snapshot reading).last_read.full_article_read) 'source activity retains exact delivered material and continuation range'
 Message @((Silent)) '刚才那段材料呢？';$capture=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json;$context=Model-CurrentEvent $capture
 Check (@($context.authorized_context|Where-Object {$_.data.observation.kind-eq'reading.observed'-and$_.data.observation.reference.scope-eq'article_source_chunk'-and$_.data.text.Contains('正文源文本')}).Count-ge1) 'program-authored reading experience keeps source scope rather than relabeling body as a lead'
 $unknown=$read.Clone();$unknown.key='Never_observed';Message @((Calls @($unknown)),(Silent));Check (!(Feedback).ok-and(Feedback).error.code-eq'activity_reference_not_observed') 'queued reading rejects a key that was never observed'
 $knownCount=(Snapshot reading).recent.Count;Message @((Calls @(@{kind='wait';seconds=10},$read)),(Silent));Message @((Silent));Check ((Api '/api/activities/status').queued-eq0-and(Snapshot reading).recent.Count-eq$knownCount) 'new input consumes no deferred reading after cancellation'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 50|ConvertFrom-Json -AsHashtable;$settings.plugins.reading.autonomousReading=$false;$null=Api '/api/settings' @{settings=$settings}
 $used=(Snapshot reading).requests_used;Message @((Calls @(@{kind='reference_search';seconds=0;query='蓝苔';language='zh'})),(Silent));Check (!(Feedback).ok-and(Feedback).error.code-in@('invalid_tool_arguments','activity_dependency_missing')-and(Snapshot reading).requests_used-eq$used-and@((Api '/api/capabilities').tools|Where-Object tool -eq reading.search).Count-eq1) 'autonomous reading has a separate operator gate while direct reference tools remain available'
 foreach($i in 1..11){$filled=Plugin atelier.write @{id=('finished-'+$i);expected_revision=0;kind='note';stage='finished';title=('已经写完的短笺'+$i);content='这是一段已完成的小创作。'};Check $filled.ok 'bounded gallery fixture adds a finished piece'}
 $scene=Snapshot atelier;$body=Body
 Check ($scene.occupied_slots-eq12-and!$scene.can_create-and@($scene.pieces|Where-Object id -eq $pieceId).Count-eq0-and@($scene.continuations|Where-Object id -eq $pieceId).Count-eq1) 'old unfinished work remains in actual continuation view after newer finished pieces fill the gallery'
 Check ($body.available_commands-notcontains'creative_note'-and$body.available_commands-contains'creative_continue'-and@((Api '/api/capabilities').tools|Where-Object tool -eq atelier.write).Count-eq1) 'full gallery removes impossible deferred creation while preserving continuation and direct versioned editing'
 $late=@{kind='creative_continue';seconds=0;piece_id=$pieceId;expected_revision=3;content='这次确实接在旧草稿之后。';stage='finished'};Message @((Calls @($late)),(Silent))
 Await {(Plugin atelier.read @{id=$pieceId}).result.piece.revision-eq4} 'older visible draft really continues';Check ((Feedback).ok) 'deferred continuation accepts a real draft tail outside the recent six metadata rows'
 Await {$a=Api '/api/activities/status';$a.queued-eq0-and$null-eq$a.active} 'older draft completion receipt settles'
 $body=Body;Check ($body.available_commands-notcontains'creative_note'-and$body.available_commands-notcontains'creative_continue') 'finished full gallery no longer advertises nonexistent creative work'
 Message @((Calls @($note)),(Silent));Check (!(Feedback).ok-and(Feedback).error.code-in@('invalid_tool_arguments','activity_dependency_missing')-and(Snapshot atelier).piece_count-eq12) 'unavailable deferred creation is rejected before a futile write attempt'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'effects finish or cancel before plugin shutdown'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Autonomous crafting passed: $checks checks. Evidence: $root"
}catch{
 $testFailure=$_;try{@{activities=Api '/api/activities/status';widget=Snapshot widget;habitat=Snapshot habitat}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $root failure-state.json) -Encoding utf8}catch{};throw $testFailure
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()};if($provider){$provider.Refresh();if(!$provider.HasExited){Stop-Process -Id $provider.Id}}}
