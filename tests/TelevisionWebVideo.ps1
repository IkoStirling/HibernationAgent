param([string]$ProjectRoot,[string]$BuildRoot,[string]$ScratchRoot,[string]$NodeExecutable,[switch]$PublicBilibili)
$ErrorActionPreference='Stop';$checks=0;$desktop=$null;$server=$null;$service=$null;$modelServer=$null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ('web-video-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null;$env:HIBERNATION_SESSION_KEY='web-video-synthetic-session'
function Check($v,$label){if(!$v){throw "CHECK FAILED: $label"};$script:checks++;Write-Output "PASS $label"}
function Await([scriptblock]$p){$until=[DateTime]::UtcNow.AddSeconds(40);while([DateTime]::UtcNow-lt$until){if(&$p){return};Start-Sleep -Milliseconds 100};throw 'web video fixture timeout'}
function Api($path,$body=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer web-video-synthetic-session'};TimeoutSec=15};if($null-ne$body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 30 -Compress))};Invoke-RestMethod @p}
function Tool($name,$arguments=@{}){
 $id=[Guid]::NewGuid().ToString('N');$until=[DateTime]::UtcNow.AddSeconds(15)
 do{try{$null=Api '/api/plugin/test' @{request_id=$id;tool=$name;args=$arguments};break}catch{if($_.Exception.Response.StatusCode.value__-ne409-or[DateTime]::UtcNow-ge$until){throw};Start-Sleep -Milliseconds 120}}while($true)
 Await {$r=Api '/api/plugin/result';$r.request_id-eq$id-and$r.state-in@('succeeded','failed')};(Api '/api/plugin/result').result
}
try{
 $server=Start-Process $NodeExecutable -ArgumentList ('"'+(Join-Path $ProjectRoot tests/TelevisionPageFixture.mjs)+'" "'+$root+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path (Join-Path $root port)};$site='http://127.0.0.1:'+(Get-Content (Join-Path $root port))
 $providerRoot=Join-Path $root model
 $modelServer=Start-Process (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path (Join-Path $providerRoot port)};$modelOrigin='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $c=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$c.port=0;$c.dataRoot=Join-Path $root data;$c.characterRoot=Join-Path $ProjectRoot character;$c.uiRoot=Join-Path $ProjectRoot ui;$c.pluginRoot=Join-Path $BuildRoot plugins;$c.profiles=@($c.profiles[0],@{id='web-watcher';label='Isolated web watcher';protocol='chat-completions';enabled=$true;origin=$modelOrigin;model='fixture-model';supportsJsonObject=$true;supportsVision=$true});$c.review=@{enabled=$false};$c.frontend.startupAnimation=$false;$c.computer=@{enabled=$false};$c.solitudeIntervalMs=604800000;$c.idleBeforeSolitudeMs=86400000
 $c.plugins=@{television=@{enabled=$true;volume=0;wakeOnEnd=$false;platforms=@(@{id='fixture';name='Local fixture';enabled=$true;hosts=@('127.0.0.1');homeUrl=$site+'/page';searchUrl=$site+'/page?q={query}'},@{id='bilibili';name='Bilibili';enabled=$true;hosts=@('bilibili.com');homeUrl='https://www.bilibili.com/';searchUrl='https://search.bilibili.com/all?keyword={query}'})};'video-analysis'=@{enabled=$true;watchWindowSeconds=10}}
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$c|ConvertTo-Json -Depth 55|Set-Content $config -Encoding utf8
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child;Await {Test-Path $ready};$script:origin='http://127.0.0.1:'+(Get-Content $ready -Raw|ConvertFrom-Json).port
 $hidden=Tool television.open_page @{url=$site+'/hidden';platform='fixture';visible=$false}
 Check ($hidden.ok-and$hidden.result.visible-eq$false) 'web page can open without showing the television'
 Await {$script:hiddenFrame=Tool video.web_observe;$script:hiddenFrame.ok}
 $hiddenPage=Tool television.page
 $hiddenTv=((Api '/api/capabilities').plugins|Where-Object id -eq television).current
 Check ($hiddenFrame.result.frames[0].image.mime_type-eq'image/jpeg'-and$hiddenTv.visible-eq$false-and$hiddenPage.result.presentation-eq'pending') 'hidden WebView extracts actual frames without applying TV layout'
 Check (Tool television.open_page @{url=$site+'/page';platform='fixture'}).ok 'real native WebView opens fixture'
 $page=Tool television.page
 Check ($page.ok-and$page.result.html.Contains('视频素材目录')-and!$page.result.screenshot_used) 'HTML navigation returns live document without a screenshot'
 Check (!$page.result.html.Contains('secret-password')-and!$page.result.html.Contains('secret-script')-and!$page.result.html.Contains('secret-token')) 'form values scripts and sensitive links are filtered'
 Check (@($page.result.links|Where-Object {$_.url-like'*/video/demo'}).Count-eq1) 'role can select an actual returned video link'
 $legacy=Tool television.observe @{include_frame=$true};Check ($legacy.ok-and!$legacy.result.image-and$legacy.result.observation_kind-eq'sanitized_live_dom') 'legacy website observe also defaults to HTML'
 $changed=Tool television.page @{expected_revision='obsolete'};Check (!$changed.ok-and$changed.error.code-eq'page_changed') 'stale HTML pagination revision rejected'
 Await {$script:frames=Tool video.web_observe @{frames=2;spacing_ms=300};$script:frames.ok}
 Check ($frames.result.sampled_frames-eq2-and$frames.result.frames[1].frame_time_seconds-gt$frames.result.frames[0].frame_time_seconds) 'video plugin reads advancing native webpage video frames'
 Check ($frames.result.frames[0].image.mime_type-eq'image/jpeg'-and!$frames.result.screenshot_used-and!$frames.result.full_video_watched-and!$frames.result.audio_understood) 'actual video frames retain precise capability scope'
 # Start at the current webpage position; wait for a real scheduler opportunity.
 $started=Tool video.watch @{action='start';source='webpage'}
 Check ($started.ok-and$started.result.session.source_kind-eq'webpage') 'website starts an actual continuous watching session'
 $session=$started.result.session.id
 Await {$script:watchStatus=((Api '/api/capabilities').plugins|Where-Object id -eq video-analysis).current.watch;$script:watchStatus.session.notice_pending}
 Check ($watchStatus.session.id-eq$session-and$watchStatus.session.unanswered_notices-ge1) 'advancing webpage playback schedules a bounded model wake'
 $next=Tool video.watch_next
 Check ($next.ok-and$next.result.watch_receipt.kind-eq'sampled_web_frames') 'watch_next obtains new webpage frames and an actual sourced receipt'
 Check ($next.result.watch_receipt.window.from-eq$next.result.frames[0].frame_time_seconds) 'receipt excludes the unsampled interval'
 $note=Tool video.note @{receipt_id=$next.result.watch_receipt.id;category='impression';text='The light moved.'}
 Check ($note.ok-and$note.result.saved) 'website watching notes save against a real frame receipt'
 $early=Tool video.watch_next;Check (!$early.ok-and$early.error.code-eq'video_watch_not_ready') 'no repeated frame polling before new playback interval'
 $null=Tool television.control @{action='pause'};Start-Sleep -Milliseconds 800
 $held=Tool video.watch_next;Check (!$held.ok-and$held.error.code-eq'video_watch_not_ready') 'paused website does not produce another watch segment'
 Check (Tool video.watch @{action='pause'}).ok 'watch task may be paused separately'
 $null=Tool television.control @{action='play'};Start-Sleep -Milliseconds 800
 Check (Tool video.watch @{action='resume'}).ok 'web watching resumes from present playback rather than skipped frames'
 # A local fixture model must itself consume TWO consecutive autonomous wakes.
 $beforeReceipts=@(((Api '/api/capabilities').plugins|Where-Object id -eq video-analysis).current.watch.receipts).Count
 $call=@{version=1;kind='call';text=$null;calls=@(@{id='next-frame';tool='video.watch_next';args=@{}})}
 $reply=@{version=1;kind='reply';text='The light moved again.';calls=@()}
 @{responses=@(@{response=$call},@{response=$reply},@{response=$call},@{response=$reply})}|ConvertTo-Json -Depth 30|Set-Content (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/model' @{id='web-watcher'}
 Await {$script:autoWatch=((Api '/api/capabilities').plugins|Where-Object id -eq video-analysis).current.watch;@($script:autoWatch.receipts).Count-ge($beforeReceipts+2)}
 Await {!(Api '/api/state').busy}
 Check ($autoWatch.receipts[-1].window.to-gt$autoWatch.receipts[-2].window.to) 'two autonomous model wake cycles consume advancing video frames without another user message'
 $captured=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 80
 $images=@($captured.messages[-1].content|Where-Object type -eq image_url)
 Check ($images.Count-eq2) 'autonomous watcher receives the actual two sampled images through model input'
 $null=Api '/api/model' @{id=$c.profiles[0].id}
 Check (Tool video.watch @{action='stop'}).ok 'web watching stops without closing television'
 $publicResult=$null
 if($PublicBilibili){
  Check (Tool television.open_page @{url='https://www.bilibili.com/video/BV1GJ411x7h7/';platform='bilibili'}).ok 'native WebView navigates to public Bilibili without saved user login'
  $p=Tool television.page;Check ($p.ok-and$p.result.html.Length-gt100) 'native Bilibili HTML extraction succeeds'
  $null=Tool television.control @{action='play'}
  Await {$script:publicFrame=Tool video.web_observe @{frames=2;spacing_ms=800};$script:publicFrame.ok-and$script:publicFrame.result.sampled_frames-eq2-and$script:publicFrame.result.frames[1].frame_time_seconds-gt$script:publicFrame.result.frames[0].frame_time_seconds}
  $publicResult=@{passed=$true;title=$p.result.title;sampled_frames=$publicFrame.result.sampled_frames;frame_times=@($publicFrame.result.frames|ForEach-Object frame_time_seconds);capture_kind=$publicFrame.result.observation_kind;anonymous=$true;direct=$true}
  $publicResult|ConvertTo-Json|Set-Content (Join-Path $root bilibili.json)
 }
 Check (Tool television.login).ok 'independent login opens normally'
 $denied=Tool television.page;Check (!$denied.ok-and$denied.error.code-eq'television_login_in_progress') 'HTML cannot inspect active login window'
 $denied=Tool video.web_observe;Check (!$denied.ok-and$denied.error.code-eq'television_login_in_progress') 'video plugin cannot inspect active login window'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(12000)-and$service.ExitCode-eq0) 'isolated video backend exits cleanly'
 @{passed=$true;checks=$checks;public_bilibili=$publicResult;paid_model_calls=0}|ConvertTo-Json -Depth 15|Set-Content (Join-Path $root acceptance.json);Write-Output "Evidence: $root"
}finally{if($modelServer-and!$modelServer.HasExited){Stop-Process -Id $modelServer.Id};if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(5000)}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id}};if($desktop){$desktop.Dispose()};if($server-and!$server.HasExited){Stop-Process -Id $server.Id}}
