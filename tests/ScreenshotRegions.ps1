param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('screenshot-regions-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='region-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(40);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer region-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plan($Responses){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8}
function Call($Id,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool='computer.screenshot';args=$Arguments})}}
function Reply{@{version=1;kind='reply';text='局部画面已取得。';calls=@()}}
function Message{$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='测试先看全图，再看局部'};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}}
function Feedback{$capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60;$last=$capture.messages[-1].content;if($last-isnot[string]){$last=$last[0].text};($last|ConvertFrom-Json).tool_results[0]}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {if($provider.HasExited){throw 'Provider exited'};Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0],@{id='vision';label='Synthetic vision';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsVision=$true;supportsJsonObject=$true})
 $cfg.review=@{enabled=$false};$cfg.plugins=@{widget=@{enabled=$true;roam=$false;skin='dinosaur'}};$cfg.computer=@{enabled=$true;allowScreenCapture=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 50|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--desktop --config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='vision'};$null=Api '/api/desktop/show' @{}
 $rectangle=@{x=0.5;y=0;width=0.5;height=0.5}
 Plan @((Call 'overview' @{target='application'}),(Call 'detail' @{target='application';region=$rectangle}),(Reply));$trace=Message;$feedback=Feedback
 Check ($feedback.ok-and$feedback.result.region.x-eq0.5-and$feedback.result.region.height-eq0.5) 'normalized rectangle survives model protocol and desktop host execution'
 Check ($feedback.result.coordinate_space-eq'normalized_full_target_top_left'-and$feedback.result.image_attached) 'actual cropped image and coordinate metadata reach model'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60
 Check ($capture.messages[-1].content[1].image_url.url.StartsWith('data:image/jpeg;base64,')) 'second screenshot is real multimodal input'
 $record=Api '/api/archive' @{id=$feedback.source_id}
 Check ($record.records[0].data.result.region.width-eq0.5-and$record.records[0].data.result.image.base64.Length-gt100) 'cropped image keeps encrypted source record for QQ delivery'
 Check ($trace.visual_inputs.Count-eq2) 'overview and detail fit existing per-turn capture budget'
 Plan @((Call 'forbidden' @{target='screen';region=$rectangle}),(Reply));$null=Message
 Check ((Feedback).error.code-eq'screen_capture_not_enabled') 'region cannot bypass full-screen permission'
 Plan @((Call 'widget-denied' @{target='widget'}),(Reply));$null=Message
 Check ((Feedback).error.code-eq'screen_capture_not_enabled') 'own-widget desktop crop still requires screen permission'
 $geometry=(Api '/api/capabilities').plugins.Where({$_.id-eq'widget'})[0].current.screen_geometry
 Check ($geometry.available-and$geometry.canvas.width-gt0-and$geometry.canvas.width-lt$geometry.desktop.width) 'widget publishes sprite rectangle instead of full overlay window'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean separate-process shutdown'
 @{completed=$true;checks=$checks;root=$root;timestamp=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Screenshot region integration passed: $checks checks. Evidence: $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}}}
