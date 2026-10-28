param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot,[Parameter(Mandatory)][string]$Python)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('style-lora-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='style-lora-local-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer style-lora-local-fixture'};TimeoutSec=15};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Plan($Responses,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@()}}
function Message{
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='它兼容吗？'};Await {!(Api '/api/chat').busy} 'turn'
 $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}
}
function Settings($Apply,$Review=$true){$v=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$v.plugins.'style-lora'.applyEnabled=$Apply;$v.review.enabled=$Review;$null=Api '/api/settings' @{settings=$v}}
function PreviewCount{$p=Join-Path $cfg.dataRoot runtime/style-lora/fixture-requests.jsonl;if(!(Test-Path $p)){return 0};@((Get-Content $p|ForEach-Object {$_|ConvertFrom-Json})|Where-Object path -eq '/preview').Count}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $pluginRoot=Join-Path $root plugins;New-Item -ItemType Directory -Force $pluginRoot|Out-Null
 Copy-Item -LiteralPath (Join-Path (Split-Path $Executable) plugins/style-lora) -Destination $pluginRoot -Recurse
 Copy-Item -LiteralPath (Join-Path $ProjectRoot tests/StyleLoraServiceFixture.py) -Destination (Join-Path $pluginRoot style-lora/worker.py) -Force
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=$pluginRoot
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsToolCalls=$true},@{id='review';label='review';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer'})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg.plugins=@{'style-lora'=@{enabled=$true;applyEnabled=$false;pythonExecutable=$Python;adapterId='style-fixture'}}
 $cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'host exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='actor'};$approve=@{decision='approve';reason='ok';guidance=''};Plan @($approve) $true
 Plan @(@{version=1;kind='call';text=$null;calls=@(@{id='capabilities';tool='capabilities.list';args=@{}})},(Reply '我不清楚，不敢瞎说。'));$trace=Message
 Check (!($trace.tool_results[0].result|ConvertTo-Json -Depth 65).Contains('style-lora')) 'operator style plugin is absent from explicit character capability inventory'
 Check ((Api '/api/chat').messages[-1].text-eq'我不清楚，不敢瞎说。'-and(PreviewCount)-eq0) 'default inactive adapter preserves original dialogue without launching editor'
 Settings $true
 Plan @((Reply '我不清楚，不敢瞎说。'));$trace=Message
 Check ($trace.style_lora.applied-and(Api '/api/chat').messages[-1].text-eq'我不清楚。') 'enabled editor feeds final approved text to chat history'
 $review=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 65;$reviewData=$review.messages[1].content|ConvertFrom-Json -Depth 65
 Check ($reviewData.style_edit.original_text-eq'我不清楚，不敢瞎说。'-and$reviewData.style_edit.proposed_text-eq'我不清楚。') 'reviewer sees original and proposed wording before approval'
 $actor=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 65
 Check (!(($actor.tools|ConvertTo-Json -Depth 65).Contains('style_lora'))) 'operator training and editing tools are absent from character catalog'
 Check (!((Api '/api/chat').messages|ConvertTo-Json -Depth 20).Contains('style-fixture')) 'adapter internals stay out of visible dialogue'
 $beforePause=Get-Content -LiteralPath (Join-Path $cfg.dataRoot runtime/style-lora/service-ready.json) -Raw|ConvertFrom-Json
 $null=Api '/api/control' @{paused=$true};Await {!(Get-Process -Id $beforePause.pid -ErrorAction SilentlyContinue)} 'paused style worker exit'
 Check (!(Get-Process -Id $beforePause.pid -ErrorAction SilentlyContinue)) 'pausing cancels the actual owned style worker'
 $null=Api '/api/control' @{paused=$false};Plan @((Reply '我不清楚，不敢瞎说。'));$trace=Message
 $afterPause=Get-Content -LiteralPath (Join-Path $cfg.dataRoot runtime/style-lora/service-ready.json) -Raw|ConvertFrom-Json
 Check ($trace.style_lora.applied-and$afterPause.instance-ne$beforePause.instance-and(Api '/api/chat').messages[-1].text-eq'我不清楚。') 'resume starts a fresh service generation and preserves reviewed editing'
 Plan @((Reply '还要2分钟。'));$trace=Message
 Check (!$trace.style_lora.applied-and$trace.style_lora.reason-eq'protected_literals_changed'-and(Api '/api/chat').messages[-1].text-eq'还要2分钟。') 'numeric mutation is rejected before semantic review'
 $before=PreviewCount;Settings $true $false;Plan @((Reply '我不清楚，不敢瞎说。'));$trace=Message
 Check (!$trace.style_lora.applied-and$trace.style_lora.reason-eq'independent_review_required'-and(PreviewCount)-eq$before) 'no automatic edit without independent review'
 Settings $true
 Plan @((Reply '我不清楚，不敢瞎说。'),(Reply '还没有核对兼容性。'));Plan @(@{decision='rewrite';reason='other';guidance='保留原意再说。'},$approve) $true
 $before=PreviewCount;$trace=Message
 Check ($trace.reviews.Count-eq2-and(PreviewCount)-eq$before+1-and(Api '/api/chat').messages[-1].text-eq'还没有核对兼容性。') 'review rewrite does not create an editor loop or replay actions'
 Plan @($approve) $true;Plan @(@{version=1;kind='call';text=$null;calls=@(@{id='forged';tool='style_lora.train';args=@{}})},(Reply '这件事由你决定。'));$trace=Message
 $requests=Get-Content -LiteralPath (Join-Path $cfg.dataRoot runtime/style-lora/fixture-requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json}
 Check (@($requests|Where-Object path -eq '/train').Count-eq0-and@($trace.tool_results|Where-Object {$_.tool-eq'style_lora.train'-and$_.ok}).Count-eq0) 'character cannot invoke operator training even when it fabricates a tool call'
 Plan @($approve) $true;Settings $false
 $before=PreviewCount;Plan @((Reply '我不清楚，不敢瞎说。'));$trace=Message
 Check ((PreviewCount)-eq$before-and(Api '/api/chat').messages[-1].text-eq'我不清楚，不敢瞎说。') 'hot disabling returns to the original path'
 $workerReady=Get-Content -LiteralPath (Join-Path $cfg.dataRoot runtime/style-lora/service-ready.json) -Raw|ConvertFrom-Json
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$settings.plugins.'style-lora'.enabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Await {!(Get-Process -Id $workerReady.pid -ErrorAction SilentlyContinue)} 'owned style worker exit'
 Check (!(Get-Process -Id $workerReady.pid -ErrorAction SilentlyContinue)) 'unloading plugin terminates its owned service'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(15000)-and$service.ExitCode-eq0) 'clean backend exit'
 @{completed=$true;checks=$checks;root=$root;fixture='synthetic service copy; real training verified separately';actual_api_calls=0}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json)
 Write-Output "Style LoRA passed: $checks checks. Evidence: $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}}}
