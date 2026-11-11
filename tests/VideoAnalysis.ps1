param([string]$ProjectRoot,[string]$BuildRoot,[string]$ScratchRoot,[string]$NodeExecutable='E:/Aliyat/Tools/VideoAnalysis/node.exe',[string]$MediaBin='E:/Aliyat/Tools/VideoAnalysis/ffmpeg-9.0.2-essentials_build/bin')
$ErrorActionPreference='Stop';$checks=0;$service=$null;$provider=$null
$root=Join-Path $ScratchRoot ('video-analysis-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:HIBERNATION_SESSION_KEY='synthetic-video-analysis-session'
function Check($v,$label){if(!$v){throw "CHECK FAILED: $label"};$script:checks++;Write-Output "PASS $label"}
function Await([scriptblock]$p){$until=[DateTime]::UtcNow.AddSeconds(70);while([DateTime]::UtcNow-lt$until){if(&$p){return};Start-Sleep -Milliseconds 80};throw 'bounded video test timed out'}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer synthetic-video-analysis-session'};TimeoutSec=10};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 25 -Compress))};Invoke-RestMethod @p}
function Tool($name,$arguments){$id=[Guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{request_id=$id;tool=$name;args=$arguments};Await {$r=Api '/api/plugin/result';$r.request_id-eq$id-and$r.state-in@('succeeded','failed')};(Api '/api/plugin/result').result}
try{
 $providerRoot=Join-Path $root provider
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path (Join-Path $providerRoot port)}
 $providerOrigin='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $c=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$c.dataRoot=Join-Path $root data;$c.characterRoot=Join-Path $ProjectRoot character;$c.uiRoot=Join-Path $ProjectRoot ui;$c.pluginRoot=Join-Path $BuildRoot plugins;$c.profiles=@($c.profiles[0]);$c.profiles+=@(@{id='actor';label='Video fixture';protocol='chat-completions';enabled=$true;origin=$providerOrigin;model='fixture-model';supportsJsonObject=$true;supportsVision=$true},@{id='reviewer';label='Video reviewer';protocol='chat-completions';enabled=$true;origin=$providerOrigin;model='fixture-reviewer';supportsJsonObject=$true;supportsVision=$true});$c.review=@{enabled=$true;profileId='reviewer';maxRewrites=1};$c.contextControl=@{preset='full'};$c.plugins=@{'video-analysis'=@{enabled=$true;nodeExecutable=$NodeExecutable;ffmpegExecutable=(Join-Path $MediaBin ffmpeg.exe);ffprobeExecutable=(Join-Path $MediaBin ffprobe.exe)}};$c.solitudeIntervalMs=604800000;$c.idleBeforeSolitudeMs=86400000;$config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$c|ConvertTo-Json -Depth 55|Set-Content $config -Encoding utf8
 $service=Start-Process -FilePath (Join-Path $BuildRoot hibernation-agent.exe) -ArgumentList ('--config "'+$config+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root stdout.log) -RedirectStandardError (Join-Path $root stderr.log);Await {if($service.HasExited){throw 'isolated backend exited'};Test-Path $ready};$script:origin='http://127.0.0.1:'+(Get-Content $ready -Raw|ConvertFrom-Json).port
 $materials=Join-Path $c.dataRoot materials;$clip=Join-Path $materials '有空格 视频.mp4'; & (Join-Path $MediaBin ffmpeg.exe) -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=320x180:rate=12' -t 5 -c:v mpeg4 $clip;if($LASTEXITCODE){throw 'synthetic video generation failed'}
 "1`n00:00:01,000 --> 00:00:03,000`n月亮灯亮起来了。`n`n2`n00:00:03,000 --> 00:00:04,800`n这是测试字幕。`n"|Set-Content (Join-Path $materials words.srt) -Encoding utf8
 $args=@{mount='materials';path='有空格 视频.mp4';from=0;to=5};$probe=Tool 'video.probe' $args;Check ($probe.ok-and$probe.result.duration-gt4.9-and$probe.result.width-eq320) 'native plugin probes actual synthetic video'
 $board=Tool 'video.storyboard' $args;Check ($board.ok-and@($board.result.frames).Count-in@(1,2)) 'native storyboard returns bounded encoded images';Check (!$board.result.full_video_watched-and!$board.result.audio_understood) 'sampling does not claim continuous viewing or audio understanding'
 Add-Type -AssemblyName System.Drawing;$bytes=[Convert]::FromBase64String($board.result.frames[0].image.base64);$stream=[IO.MemoryStream]::new($bytes);$image=[Drawing.Image]::FromStream($stream);try{Check ($image.Width-eq$board.result.frames[0].width-and$image.Height-eq$board.result.frames[0].height) 'returned PNG decodes to declared image geometry'}finally{$image.Dispose();$stream.Dispose()}
 $zoom=Tool 'video.zoom' $args;Check ($zoom.ok-and$zoom.result.sampled_frames-ge4) 'zoom expands the window to at least four samples'
 $subArgs=$args.Clone();$subArgs.transcript_path='words.srt';$subArgs.query='月亮';$found=Tool 'video.locate' $subArgs;Check ($found.ok-and$found.result.matches[0].from-eq1-and$found.result.subtitle_source-eq'provided_subtitle') 'subtitle lookup retains source and absolute timestamps'
 $subArgs.Remove('query');$withText=Tool 'video.storyboard' $subArgs;Check ($withText.ok-and$withText.result.text-match'月亮灯') 'storyboard aligns provided Chinese subtitles with visual cells'
 foreach($bad in @('../archive/records.sqlite','C:/Windows/win.ini','https://example.com/video.mp4')){$badArgs=$args.Clone();$badArgs.path=$bad;$failure=Tool 'video.probe' $badArgs;Check (!$failure.ok) "unauthorized input rejected: $bad"}
 Set-Content (Join-Path $materials fake.mp4) '#EXTM3U' -Encoding ascii
 $disguised=$args.Clone();$disguised.path='fake.mp4';$failure=Tool 'video.probe' $disguised;Check (!$failure.ok-and$failure.error.code-eq'video_source_denied') 'disguised playlist is rejected before decoder invocation'
 $long=$args.Clone();$long.to=121;$failure=Tool 'video.storyboard' $long;Check (!$failure.ok) 'overlong window is rejected before processing'
 $badMount=$args.Clone();$badMount.mount='archive';$failure=Tool 'video.probe' $badMount;Check (!$failure.ok) 'private archive mount cannot be selected'
 Check (@(Get-ChildItem (Join-Path $c.dataRoot runtime/video-analysis) -Directory -ErrorAction SilentlyContinue).Count-eq0) 'processing scratch directories are removed after completion'

 $null=Api '/api/model' @{id='actor'}
 $call=@{version=1;kind='call';text=$null;calls=@(@{id='board';tool='video.storyboard';args=$args})}
 $reply=@{version=1;kind='reply';text='画面里的变化在这段分镜里。';calls=@()}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$call},@{response=$reply})}|ConvertTo-Json -Depth 30|Set-Content (Join-Path $providerRoot plan.json) -Encoding utf8
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $providerRoot review-plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='分析素材区这段合成测试视频。'};Await {!(Api '/api/chat').busy}
 Check (!(Api '/api/chat').call_failed) 'role can invoke the new video tool through normal dialogue'
 $actor=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 65
 $reviewer=Get-Content (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 65
 $actorImages=@($actor.messages[-1].content|Where-Object type -eq image_url);$reviewImages=@($reviewer.messages[-1].content|Where-Object type -eq image_url)
 Check ($actorImages.Count-ge1-and$actorImages.Count-le2) 'role request receives actual storyboard image parts'
 Check ($actorImages.Count-eq$reviewImages.Count-and$actorImages[0].image_url.url-eq$reviewImages[0].image_url.url) 'review receives the identical storyboard image evidence'
 Check (!$actor.messages[-1].content[0].text.Contains('base64')) 'structured feedback contains metadata instead of inline pixel text'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(12000)-and$service.ExitCode-eq0) 'isolated backend exits cleanly'
 @{passed=$true;checks=$checks;paid_model_calls=0;real_accounts_used=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output "Evidence: $root"
}finally{if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id};if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(5000)}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id}}}
