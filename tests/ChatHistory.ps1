param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/release};$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs);Add-Type -Path (Join-Path $PSScriptRoot NativeChatTest.cs);Add-Type -Path (Join-Path $PSScriptRoot BubbleLayoutProbe.cs)
$root=Join-Path $ProjectRoot ('artifacts/chat-history-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null;New-Item -ItemType File (Join-Path $root .synthetic-chat-history)|Out-Null;$desktop=$null;$service=$null;$provider=$null;$checks=0
$node=if($env:HA_NODE_RUNTIME){$env:HA_NODE_RUNTIME}else{'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'}
$env:HIBERNATION_SESSION_KEY='chat-history-fixture';$env:TEMP=Join-Path $BuildRoot tmp;$env:TMP=$env:TEMP
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){
 $end=[DateTime]::UtcNow.AddSeconds(25)
 while([DateTime]::UtcNow-lt$end){
  try{if(&$Probe){return}}catch{
   # Backend readiness precedes native RichEdit creation/layout. A busy UI is
   # retried within the same deadline; other probe failures remain fatal.
   if(!$_.Exception.ToString().Contains('owned_bubble_timeout')){throw}
  }
  Start-Sleep -Milliseconds 30
 }
 throw "TIMEOUT $Name"
}
function Api($Path,$Body=$null){$a=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer chat-history-fixture'};TimeoutSec=10};if($null-ne$Body){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($Body|ConvertTo-Json -Compress -Depth 20))};Invoke-RestMethod @a}
try{
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.plugins=@{};$cfg.profiles=@($cfg.profiles[0]);$cfg.frontend=@{startupAnimation=$false;reducedMotion=$true};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$config=Join-Path $root config.json;$cfg|ConvertTo-Json -Depth 30|Set-Content -LiteralPath $config -Encoding utf8
 $ffmpeg=if($env:HA_TEST_FFMPEG){$env:HA_TEST_FFMPEG}else{'E:/MySoftware/_FormatFactory/FormatFactory/ffmpeg.exe'};& $ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc=size=64x48:rate=10' -t 1 -c:v libvpx -an (Join-Path $root preview.webm);if($LASTEXITCODE-ne0){throw 'synthetic video generation failed'}
 & python -c 'from PIL import Image; import sys; from pathlib import Path; p=Path(sys.argv[1]); a=Image.new("RGB",(32,32),"red"); b=Image.new("RGB",(32,32),"blue"); a.save(p/"preview.png"); a.save(p/"animated.gif",save_all=True,append_images=[b],duration=300,loop=0)' $root
 if($LASTEXITCODE-ne0){throw 'synthetic image generation failed'}
 $seed=& (Join-Path $BuildRoot Hibernation_ChatHistoryFixture.exe) $config (Join-Path $root preview.png) (Join-Path $root animated.gif) (Join-Path $root preview.webm);if($LASTEXITCODE-ne0){throw 'encrypted history fixture failed'};$seed|Set-Content (Join-Path $root fixture.json)
 Remove-Item -LiteralPath (Join-Path $root preview.png),(Join-Path $root animated.gif) -Force
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,(Join-Path $root ready.json),$ProjectRoot);$service=$desktop.Child;Await {if($service.HasExited){throw 'private fixture exited'};Test-Path (Join-Path $root ready.json)} 'native ready';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
 $null=Api '/api/desktop/show' @{}; $main=$desktop.Find('HibernationConversationV2');$viewport=[ChatWindowTest]::GetDlgItem($main,1203);Await {@([BubbleLayoutProbe]::Inspect($viewport,$service.Id)).Count-eq24} 'bounded native controls';Check (@([BubbleLayoutProbe]::Inspect($viewport,$service.Id)).Count-eq24) 'native conversation creates 24 bubbles for 202 records'
 $geometry=[BubbleLayoutProbe]::Inspect($viewport,$service.Id);$geometry|Select-Object Length,Height,Width,LastY,Lines|ConvertTo-Json|Set-Content (Join-Path $root initial-geometry.json)
 Check (@($geometry|Where-Object {$_.Length-lt50-and$_.Height-gt70}).Count-eq0) 'loaded short history bubbles stay compact'
 $null=[ChatWindowTest]::Send($main,0x111,1206);Await {([BubbleLayoutProbe]::Inspect($viewport,$service.Id)|ForEach-Object {[ChatWindowTest]::Text($_.Window)})-contains'synthetic conversation 154'} 'older native page';Check ([ChatWindowTest]::IsWindowEnabled([ChatWindowTest]::GetDlgItem($main,1207))) 'older page offers return to recent'
 $null=[ChatWindowTest]::Send($main,0x111,1207);Await {([BubbleLayoutProbe]::Inspect($viewport,$service.Id)|ForEach-Object {[ChatWindowTest]::Text($_.Window)})-match'synthetic attachments'} 'latest native page'
 $page=Api '/api/chat/page' @{};Check ($page.total-eq202-and$page.messages.Count-eq24-and$page.has_more) 'history index survives restart beyond the context index limit'
 $ids=@();$cursor='';do{$page=Api '/api/chat/page' @{before=$cursor;limit=24};$ids+=@($page.messages.id);$cursor=$page.next_before}while($page.has_more);Check ($ids.Count-eq202-and@($ids|Select-Object -Unique).Count-eq202-and$ids-notcontains'private-tool') 'paging returns each approved dialogue once and excludes tool traffic'
 foreach($invalid in @(@{limit=49},@{limit=4294967320},@{limit=-1},@{limit='24'},@{before='unknown'})){try{$null=Api '/api/chat/page' $invalid;throw 'invalid pagination accepted'}catch{Check ($_.Exception.Response.StatusCode.value__-in@(400,404)) 'invalid cursor or page size rejected'}}
 & $node (Join-Path $ProjectRoot tests/ChatHistoryBrowser.mjs) $script:origin $root;if($LASTEXITCODE-ne0){throw 'history media browser checks failed'}
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'native history shuts down cleanly';$raw=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $root data/archive/records.sqlite)));Check (!$raw.Contains('synthetic attachments')-and!$raw.Contains('preview.png')) 'dialogue and attachment names are absent from plaintext database bytes';@{completed=$true;checks=$checks;root=$root;production_archive_read=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json);Write-Output ('Chat history passed: '+$root)
}finally{if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{}};if($desktop){$desktop.Dispose()};if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id}}
