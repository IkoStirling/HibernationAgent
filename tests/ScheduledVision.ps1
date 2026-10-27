param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('scheduled-vision-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='scheduled-vision-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=25){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 70};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer scheduled-vision-fixture'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Calls($Id,$Tool,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=$Id;tool=$Tool;args=$Arguments})}}
try{
 $providerRoot=Join-Path $root provider;$provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.review=@{enabled=$false}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Scheduled vision fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsVision=$true;supportsJsonObject=$true})
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 Add-Type -AssemblyName System.Drawing
 $bitmap=[Drawing.Bitmap]::new(480,320,[Drawing.Imaging.PixelFormat]::Format32bppArgb);$rectangle=[Drawing.Rectangle]::new(0,0,480,320);$locked=$bitmap.LockBits($rectangle,[Drawing.Imaging.ImageLockMode]::WriteOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
 try{$noise=[byte[]]::new([Math]::Abs($locked.Stride)*320);[Random]::new(17).NextBytes($noise);for($i=3;$i-lt$noise.Length;$i+=4){$noise[$i]=255};[Runtime.InteropServices.Marshal]::Copy($noise,0,$locked.Scan0,$noise.Length)}finally{$bitmap.UnlockBits($locked)}
 try{$bitmap.Save((Join-Path $cfg.dataRoot materials/large-image.png),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
 $silent=@{version=1;kind='silence';text=$null;calls=@()}
 $items=@((Calls 'image' 'workspace.view_image' @{mount='materials';path='large-image.png'}),(Calls 'later' 'schedule.create' @{id='after-image';topic='稍后考虑这张实际提供过的图片来源，允许安静；不重新附图。';after_ms=60000;source_ids=@('${last_source_id}')}),$silent)
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@($items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='检查图片后安排一次有来源的稍后独处'};Await {!(Api '/api/chat').busy} 'foreground';Check (!(Api '/api/chat').call_failed) 'image and delayed source proposal complete'
 $job=@((Api '/api/state').jobs|Where-Object id -eq routine.after-image)[0];Check ($job-and$job.state-eq'pending') 'real source-backed routine is queued for the minimum one-minute delay'
 # Pixels belong to the first request after observation, not the later finish.
 $foreground=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 65
 $received=@(Get-Content -LiteralPath (Join-Path $providerRoot requests.jsonl)|ForEach-Object {$_|ConvertFrom-Json -Depth 65}|ForEach-Object {$_.messages}|Where-Object {$_.content-isnot[string]}|ForEach-Object {$_.content}|Where-Object type -eq image_url)
 Check ($received.Count-eq1-and$received[0].image_url.url.Length-gt50000) 'fixture contains a genuine large image rather than a tiny placeholder'
 Check (($foreground|ConvertTo-Json -Depth 70).IndexOf('base64')-lt0) 'successful visual delivery is not repeated in the final foreground request'
 Await {$script:finished=@((Api '/api/state').jobs|Where-Object id -eq routine.after-image)[0];$script:finished.state-in@('succeeded','failed')} 'actual scheduled execution' 75
 $result=$finished.result|ConvertFrom-Json;Check ($finished.state-eq'succeeded'-and$result.status-eq'completed') 'one-minute routine really runs with its authorized visual source'
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 65;$context=$capture.messages[1].content|ConvertFrom-Json -Depth 60
 $visual=@($context.authorized_context|Where-Object {$_.data.context_representation-eq'observation_metadata_without_pixels'})
 Check ($context.event_kind-eq'solitude'-and$visual.Count-eq1-and$visual[0].data.result.image_available_in_source-and!$visual[0].data.result.image_content_in_context) 'scheduled role receives referenced provenance without pretending pixels were reattached'
 Check (($capture|ConvertTo-Json -Depth 70).IndexOf('base64')-lt0-and$context.perception.images_in_current_request-eq0) 'scheduled request never embeds large image bytes as JSON text'
 $raw=Api '/api/archive' @{id=$visual[0].id};Check ($raw.records[0].data.result.image.base64.Length-gt50000) 'original image remains intact in encrypted archive after delayed use'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'scheduled vision host stops cleanly'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Scheduled vision passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
