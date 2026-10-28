param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('pixel-model-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='pixel-model-synthetic'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(35);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer pixel-model-synthetic'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Plan($Responses,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 65|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Calls($Items){@{fixture_native_calls=$Items}}
function Call($Id,$Name,$Arguments){@{id=$Id;name=('ha_'+$Name.Replace('_','_US_').Replace('.','_DOT_'));arguments=$Arguments}}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@()}}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
try{
 $binaryRoot=Split-Path $Executable;$runtimeRoot=Join-Path $root runtime;New-Item -ItemType Directory -Force $runtimeRoot|Out-Null
 Copy-Item -LiteralPath $Executable -Destination $runtimeRoot
 Copy-Item -LiteralPath $ProviderExecutable -Destination $runtimeRoot
 Get-ChildItem -LiteralPath $binaryRoot -File -Filter '*.dll'|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $runtimeRoot}
 $Executable=Join-Path $runtimeRoot 'hibernation-agent.exe';$ProviderExecutable=Join-Path $runtimeRoot 'Hibernation_TestProvider.exe'
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {if($provider.HasExited){throw 'provider exited'};Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider'
 $fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.draftsRoot=Join-Path $root drafts;$cfg.materialsRoot=Join-Path $root materials
 $cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $binaryRoot plugins
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Actor fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsToolCalls=$true;supportsVision=$true;supportsStreaming=$false},@{id='review';label='Review fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsVision=$true})
 $cfg.plugins=@{'pixel-canvas'=@{enabled=$true}};$cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg.computer=@{enabled=$false}
 $cfg.maxStartsPerHour=1000;$cfg.maxToolRounds=8;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 65|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='actor'}
 $create=@{id='little-lamp';title='一盏小灯';width=16;height=16;palette=@('#00000000','#e8bf60ff','#506448ff')}
 $edit=@{id='little-lamp';expected_revision=1;frame=0;commands=@(@{op='rect';x=5;y=3;x2=10;y2=8;color=1;filled=$true},@{op='line';x=7;y=9;x2=7;y2=12;color=2})}
 $approve=@{decision='approve';reason='ok';guidance=''};$rewrite=@{decision='rewrite';reason='other';guidance='保留已经成功的操作，只修改对白，不要重做画布。'}
 Plan @($approve,$approve,$rewrite,$approve,$approve) $true
 Plan @((Calls @((Call 'create' 'pixel.create' $create),(Call 'draw' 'pixel.edit' $edit))),
  (Calls @((Call 'look' 'pixel.preview' @{id='little-lamp'}),(Call 'export' 'pixel.export' @{id='little-lamp';expected_revision=2;format='png'}),(Call 'read' 'pixel.read' @{id='little-lamp'}))),
  (Reply '我把灯画好了。'),(Calls @((Call 'create-again' 'pixel.create' $create),(Call 'draw-again' 'pixel.edit' $edit))),
  (Reply '画了盏小灯，先放在这里。'))
 $trace=Message '试着画一盏小灯，自己看一眼，再保存成图片。'
 $trace|ConvertTo-Json -Depth 90|Set-Content -LiteralPath (Join-Path $root trace.json) -Encoding utf8
 Check ($trace.status-eq'succeeded'-or$trace.outcome-eq'succeeded'-or(Api '/api/chat').messages[-1].text-eq'画了盏小灯，先放在这里。') 'character completes the actual native-tool creative flow'
 $actor=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 80
 Check (@($actor.tools|Where-Object {$_.function.name-eq'ha_pixel_DOT_preview'}).Count-eq1) 'character discovers pixel tools in the normal capability catalog'
 $rows=@($actor.messages|Where-Object role -eq tool|ForEach-Object {$_.content|ConvertFrom-Json -Depth 80})
 $drawn=@($rows|Where-Object {$_.result.document.id-eq'little-lamp'})
 Check ($drawn.Count-ge2-and@($drawn|Where-Object {($_.result.document.frames|ConvertTo-Json -Depth 20)-match '"pixels"'}).Count-eq0) 'model receipts preserve document identity without full indexed grids'
 $attached=@($trace.visual_inputs|Where-Object {$_.attached-and$_.delivery_state-eq'acknowledged'})
 Check ($attached.Count-ge1) 'actual generated preview pixels were acknowledged by the model fixture'
 $review=Get-Content -LiteralPath (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 80
 $images=@($review.messages|Where-Object {$_.content-is[array]}|ForEach-Object {$_.content}|Where-Object type -eq image_url)
 Check ($images.Count-ge1) 'semantic reviewer receives the same actual artwork image'
 $stored=Get-Content -LiteralPath (Join-Path $cfg.draftsRoot 'pixel-canvas/documents/little-lamp.json') -Raw|ConvertFrom-Json -Depth 65
 Check ($stored.document.revision-eq2) 'review rewrite does not repaint or recreate the saved document'
 Check (@($trace.tool_results|Where-Object reused_after_review).Count-eq2) 'successful create and edit calls are reused after reviewer rejection'
 $exports=@(Get-ChildItem -LiteralPath (Join-Path $cfg.draftsRoot pixel-canvas/exports) -File -Filter '*.png')
 Check ($exports.Count-eq1) 'actual PNG export remains one versioned artifact'
 $readSource=@($trace.tool_results|Where-Object tool -eq 'pixel.read')[0].source_id
 $raw=Api '/api/archive' @{id=$readSource};Check ($raw.records[-1].data.result.document.frames[0].pixels.Count-eq256) 'encrypted original observation preserves full editable pixels'
 Plan @($approve) $true;Plan @((Calls @((Call 'revisit-artwork' 'observation.revisit' @{source_id=$readSource}))),(Reply '那盏灯还在。'));$revisited=Message '刚才画了什么？'
 Check (@($revisited.tool_results|Where-Object {$_.ok-and$_.tool-eq'observation.revisit'}).Count-eq1) 'character can revisit its authorized past artwork observation'
 $later=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 80
 $revisitRows=@($later.messages|Where-Object role -eq tool|ForEach-Object {$_.content|ConvertFrom-Json -Depth 80})
 $revisitReceipt=@($revisitRows|Where-Object {$_.result.original_tool-eq'pixel.read'})[0]
 Check ($revisitReceipt.ok-and$revisitReceipt.source_id-eq$readSource-and!($revisitReceipt.result.document|ConvertTo-Json -Depth 20).Contains('"pixels"')) 'revisit preserves original identity and time without restoring raw grids to model context'
 $text=@($later.messages|Where-Object {$_.role-ne'system'-and$_.content-is[string]}|ForEach-Object content)-join "`n"
 Check ($text.Contains('little-lamp')-and!$text.Contains('"pixels":')) 'later conversation keeps actual artwork continuity without repeating grids'
 Check (!(Api '/api/chat').messages[-1].text.Contains('pixel.')) 'tool protocol stays out of the visible role reply'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 65|ConvertFrom-Json -AsHashtable;$settings.plugins.'pixel-canvas'.enabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Check (@((Api '/api/capabilities').tools|Where-Object {$_.tool.StartsWith('pixel.')} ).Count-eq0) 'disabling plugin removes its role capabilities'
 Check (Test-Path -LiteralPath (Join-Path $cfg.draftsRoot 'pixel-canvas/documents/little-lamp.json')) 'disabling plugin preserves authored artwork'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(15000)-and$service.ExitCode-eq0) 'clean independent host shutdown'
 @{completed=$true;checks=$checks;source='synthetic native-tool actor/reviewer; no paid API';private_data=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json)
 Write-Output "Pixel model integration passed: $checks checks. Evidence: $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}}}
