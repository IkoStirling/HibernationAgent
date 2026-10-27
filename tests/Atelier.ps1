param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$checks=0
$root=Join-Path $ScratchRoot ('atelier-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='atelier-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer atelier-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plugin($Tool,$Arguments=@{}){
 $null=Api '/api/plugin/test' @{request_id=[Guid]::NewGuid().ToString('N');tool=$Tool;args=$Arguments}
 Await {$status=Api '/api/plugin/result';$status.state-ne'pending'-and$status.state-ne'running'} 'plugin'
 (Api '/api/plugin/result').result
}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'backend exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'atelier host shutdown'}
try{
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0]);$cfg.plugins=@{atelier=@{enabled=$true;maximumPieces=4}};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.review=@{enabled=$false}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $configPath -Encoding utf8
 Start-Backend
 Check ((Plugin atelier.list).result.pieces.Count-eq0) 'creative corner starts empty without synthetic experiences'
 $piece=@{id='late-moon';expected_revision=0;title='慢半拍的月亮';kind='story';stage='draft';content='月亮走到窗边，才发现自己忘了带影子。'}
 $r=Plugin atelier.write $piece;Check ($r.ok-and$r.result.completed-and$r.result.piece.revision-eq1) 'new piece writes an actual bounded draft'
 $r=Plugin atelier.read @{id='late-moon'};Check ($r.result.piece.content-eq$piece.content-and$r.result.content_kind-eq'authored_draft') 'creative content reads back with explicit fiction provenance'
 $r=Plugin atelier.write $piece;Check (!$r.ok-and$r.error.code-eq'atelier_revision_conflict') 'stale edit cannot overwrite the draft'
 $addition='后来，影子自己敲了敲门。';$r=Plugin atelier.append @{id=$piece.id;expected_revision=1;content=$addition;stage='draft'};$piece.content+="`n"+$addition
 Check ($r.ok-and$r.result.piece.revision-eq2-and(Plugin atelier.read @{id=$piece.id}).result.piece.content-eq$piece.content) 'append retains the original text and commits a new version'
 $r=Plugin atelier.append @{id=$piece.id;expected_revision=1;content='旧版本不得覆盖';stage='draft'};Check (!$r.ok-and$r.error.code-eq'atelier_revision_conflict') 'stale append has no write effect'
 $r=Plugin atelier.append @{id=$piece.id;expected_revision=2;content='   ';stage='draft'};Check (!$r.ok-and$r.error.code-eq'atelier_empty_content') 'blank continuation is rejected'
 $preview=(Api '/api/capabilities').plugins.Where({$_.id-eq'atelier'})[0].current.continuations[0]
 Check ($preview.id-eq$piece.id-and$preview.revision-eq2-and$preview.tail-eq$piece.content-and!$preview.tail_is_excerpt) 'unfinished work exposes an actual bounded continuation excerpt'
 $piece.expected_revision=2;$piece.content+=' 它决定先坐一会儿。';$piece.stage='finished';$r=Plugin atelier.write $piece
 Check ($r.result.piece.revision-eq3-and$r.result.piece.stage-eq'finished') 'continuation preserves a later version and completed stage'
 $r=Plugin atelier.append @{id=$piece.id;expected_revision=3;content='不应继续已完成作品';stage='draft'};Check (!$r.ok-and$r.error.code-eq'atelier_piece_not_draft') 'cached append cannot silently reopen finished work'
 $bad=$piece.Clone();$bad.id='../persona';$r=Plugin atelier.write $bad;Check (!$r.ok-and$r.error.code-eq'atelier_identity') 'creative ID cannot escape its subdirectory'
 $corrupt=Join-Path $cfg.dataRoot drafts/atelier/broken.json;[IO.File]::WriteAllText($corrupt,'human edit is not a valid piece')
 $bad=$piece.Clone();$bad.id='broken';$bad.expected_revision=0;$r=Plugin atelier.write $bad;Check (!$r.ok-and$r.error.code-eq'atelier_existing_piece_invalid') 'unknown existing text is not silently overwritten'
 Check ((Plugin atelier.list).result.pieces.Count-eq1) 'malformed external file does not poison listing'
 foreach($id in @('small-note','window-sketch')){$new=$piece.Clone();$new.id=$id;$new.expected_revision=0;if($id-eq'window-sketch'){$new.stage='draft';$new.content='细'*3000};$r=Plugin atelier.write $new;Check $r.ok 'another independent creative piece'}
 $r=Plugin atelier.append @{id='window-sketch';expected_revision=1;content='尾';stage='draft'};Check (!$r.ok-and$r.error.code-eq'atelier_content_limit'-and(Plugin atelier.read @{id='window-sketch'}).result.piece.revision-eq1) 'combined content limit rejects append without replacing prior text'
 $preview=(Api '/api/capabilities').plugins.Where({$_.id-eq'atelier'})[0].current.continuations[0]
 Check ($preview.id-eq'window-sketch'-and$preview.tail_is_excerpt-and$preview.tail-eq('细'*200)) 'long draft tail remains bounded at valid UTF-8 character boundaries'
 $new=$piece.Clone();$new.id='too-many';$new.expected_revision=0;$r=Plugin atelier.write $new;Check (!$r.ok-and$r.error.code-eq'atelier_piece_limit') 'piece limit counts existing unavailable files too'
 $capacity=(Api '/api/capabilities').plugins.Where({$_.id-eq'atelier'})[0].current;Check ($capacity.piece_count-eq3-and$capacity.occupied_slots-eq4-and!$capacity.can_create) 'cached capacity includes unavailable files without advertising a free slot'
 [IO.File]::WriteAllText((Join-Path $cfg.dataRoot drafts/atelier/broken.json),'{"version":"wrong-type"}');$listed=Plugin atelier.list;Check ($listed.ok-and$listed.result.pieces.Count-eq3) 'wrong-typed external version cannot poison the creative listing'
 $unavailable=Plugin atelier.read @{id='broken'};Check (!$unavailable.ok-and$unavailable.error.code-eq'atelier_piece_unavailable') 'malformed draft is unavailable rather than a plugin exception'
 Stop-Backend;Start-Backend
 $r=Plugin atelier.read @{id='late-moon'};Check ($r.result.piece.revision-eq3-and$r.result.piece.content-eq$piece.content) 'creative continuity survives process restart'
 $r=Plugin atelier.list;Check ($r.result.pieces.Count-eq3) 'remaining pieces are discovered from authorized files'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 50|ConvertFrom-Json -AsHashtable;$settings.plugins.atelier.enabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Check (@((Api '/api/capabilities').tools|Where-Object {$_.tool-like'atelier.*'}).Count-eq0) 'disabling plugin removes its model tools'
 Stop-Backend
 Check ((Get-Content -LiteralPath (Join-Path $cfg.dataRoot drafts/atelier/late-moon.json) -Raw|ConvertFrom-Json).content-eq$piece.content) 'disabling preserves authored drafts'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Atelier passed: $checks checks. Evidence: $root"
}finally{if($service){$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}}
