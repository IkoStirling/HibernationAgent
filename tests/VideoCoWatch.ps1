param([string]$ProjectRoot,[string]$BuildRoot,[string]$ScratchRoot,[string]$NodeExecutable,[string]$MediaBin)
$ErrorActionPreference='Stop';$checks=0;$desktop=$null;$service=$null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ('co-watch-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:HIBERNATION_SESSION_KEY='co-watch-synthetic-session'
function Check($v,$label){if(!$v){throw "CHECK FAILED: $label"};$script:checks++;Write-Output "PASS $label"}
function Await([scriptblock]$p){$until=[DateTime]::UtcNow.AddSeconds(45);while([DateTime]::UtcNow-lt$until){if(&$p){return};Start-Sleep -Milliseconds 100};throw 'bounded co-watch test timed out'}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer co-watch-synthetic-session'};TimeoutSec=10};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @p}
function Tool($name,$arguments=@{}){
 $id=[Guid]::NewGuid().ToString('N');$until=[DateTime]::UtcNow.AddSeconds(15)
 do{try{$null=Api '/api/plugin/test' @{request_id=$id;tool=$name;args=$arguments};break}catch{if($_.Exception.Response.StatusCode.value__-ne409-or[DateTime]::UtcNow-ge$until){throw};Start-Sleep -Milliseconds 120}}while($true)
 Await {$r=Api '/api/plugin/result';$r.request_id-eq$id-and$r.state-in@('succeeded','failed')};(Api '/api/plugin/result').result
}
function CurrentWatch{((Api '/api/capabilities').plugins|Where-Object id -eq video-analysis).current.watch}
function StartBackend{
 $script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$script:config,$script:ready,$ProjectRoot);$script:service=$script:desktop.Child
 Await {if($script:service.HasExited){throw 'isolated co-watch backend exited'};Test-Path $script:ready};$script:origin='http://127.0.0.1:'+(Get-Content $script:ready -Raw|ConvertFrom-Json).port
}
try{
 $c=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$c.dataRoot=Join-Path $root data;$c.characterRoot=Join-Path $ProjectRoot character;$c.uiRoot=Join-Path $ProjectRoot ui;$c.pluginRoot=Join-Path $BuildRoot plugins;$c.profiles=@($c.profiles[0]);$c.review=@{enabled=$false};$c.frontend.startupAnimation=$false;$c.computer=@{enabled=$false};$c.plugins=@{television=@{enabled=$true;volume=0;wakeOnEnd=$false};'video-analysis'=@{enabled=$true;watchWindowSeconds=10;asrEnabled=$false;nodeExecutable=$NodeExecutable;ffmpegExecutable=(Join-Path $MediaBin ffmpeg.exe);ffprobeExecutable=(Join-Path $MediaBin ffprobe.exe)}};$c.solitudeIntervalMs=604800000;$c.idleBeforeSolitudeMs=86400000
 $script:config=Join-Path $root config.json;$script:ready=Join-Path $root ready.json;$c|ConvertTo-Json -Depth 55|Set-Content $script:config -Encoding utf8
 StartBackend
 $materials=Join-Path $c.dataRoot materials;$clip=Join-Path $materials clip.mp4
 & (Join-Path $MediaBin ffmpeg.exe) -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=160x90:rate=12' -t 14 -c:v mpeg4 $clip;if($LASTEXITCODE){throw 'synthetic video generation failed'}
 "1`n00:00:01,000 --> 00:00:02,000`n灯亮了。`n`n2`n00:00:09,000 --> 00:00:13,500`n这句台词包含后续内容。`n"|Set-Content (Join-Path $materials words.srt) -Encoding utf8
 $failure=Tool video.watch @{action='start';mount='materials';path='clip.mp4'};Check (!$failure.ok) 'cannot start against a closed television'
 Check (Tool television.open @{source='clip.mp4'}).ok 'uses real engine television playback on inactive test desktop'
 Await {(Tool video.watch @{action='start';mount='materials';path='clip.mp4';transcript_path='words.srt'}).ok}
 $failure=Tool video.storyboard @{mount='materials';path='clip.mp4';from=0;to=14};Check (!$failure.ok-and$failure.error.code-eq'video_watch_outside_observed_progress') 'active co-watch cannot preview future frames'
 Await {(CurrentWatch).session.coverage.to-ge10.5}
 Await {@((Api '/api/state').jobs|Where-Object id -eq event).Count-ge1}
 Check ((CurrentWatch).session.unanswered_notices-ge1) 'playback progress independently wakes a background model event' 
 Check (Tool television.control @{action='pause'}).ok 'player pause succeeds'
 Start-Sleep -Milliseconds 350
 Check (!(Tool video.watch_next).ok) 'paused playback does not start next analysis'
 Check (Tool television.control @{action='play'}).ok 'player resumes without resetting task'
 Await {$script:board=Tool video.watch_next;if(!$script:board.ok-and$script:board.error.code-ne'video_watch_not_ready'){throw ('Unexpected next-window failure: '+$script:board.error.code)};$script:board.ok}
 $board=$script:board
 Check ($board.ok-and$board.result.watch_receipt.id) 'next observed window delivers native storyboard and real receipt'
 Check (!$board.result.text.Contains('这句台词包含后续内容')) 'subtitle crossing observed boundary is withheld'
 $receipt=$board.result.watch_receipt
 Check (Tool video.note @{receipt_id=$receipt.id;category='impression';text='这盏灯让我想多看一会儿。'}).ok 'impression saved against exact video receipt'
 Check (!(Tool video.note @{receipt_id='invented';category='event';text='编造来源'}).ok) 'invented note source rejected by native plugin'
 Await {((Api '/api/capabilities').plugins|Where-Object id -eq television).current.state-eq'ended'}
 $tail=Tool video.watch_next;Check $tail.ok 'final short remainder is consumed after actual playback ends';Check ($tail.result.text.Contains('这句台词包含后续内容')) 'withheld crossing subtitle appears once its full line has actually played'
 Check ((CurrentWatch).session.status-eq'completed') 'task reaches completed only after final analysis'
 Check ((Tool video.notes).result.notes.Count-eq1) 'watch notes available to later dialogue tools'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'watch state shuts down cleanly'
 $desktop.Dispose();$desktop=$null;$service=$null;Remove-Item -LiteralPath $ready -ErrorAction SilentlyContinue
 StartBackend
 $saved=CurrentWatch;Check ($saved.notes.Count-eq1-and$saved.notes[0].source.id-eq$receipt.id) 'encrypted archive restores note text and exact source receipt'
 Check ($saved.session.status-eq'completed') 'restart does not silently restart completed viewing'
 $null=Api '/api/shutdown' @{};$null=$service.WaitForExit(10000)
 @{passed=$true;checks=$checks;paid_model_calls=0;real_accounts_used=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output "Evidence: $root"
}finally{if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(5000)}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()}}
