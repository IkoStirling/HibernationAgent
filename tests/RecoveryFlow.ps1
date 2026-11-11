param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='')
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ProjectRoot ('artifacts/recovery-flow-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:HIBERNATION_SESSION_KEY='recovery-flow-fixture';$env:TEMP=Join-Path $root tmp;$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null
$provider=$null;$desktop=$null;$service=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=25){$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 70};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer recovery-flow-fixture'};TimeoutSec=15};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 45|Set-Content -LiteralPath (Join-Path $root provider/plan.json) -Encoding utf8}
function Call($Name,$Arguments){@{version=1;kind='call';text=$null;calls=@(@{id=[Guid]::NewGuid().ToString('N');tool=$Name;args=$Arguments})}}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@();presentation=@{speech=$false;bubble=$false}}}
function Message($Text){$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'dialogue';Check (!(Api '/api/chat').call_failed) 'dialogue completed'}
function Requests{if(Test-Path -LiteralPath (Join-Path $root provider/requests.jsonl)){return @(Get-Content -LiteralPath (Join-Path $root provider/requests.jsonl)).Count};0}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'recovery host exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown';$desktop.Dispose();$script:desktop=$null}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $root provider/port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $root provider/port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000;$cfg.maxToolRounds=3
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Recovery fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsJsonObject=$true})
 $cfg.computer=@{enabled=$true;allowScreenCapture=$true;readRoots=@()}
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;autonomyRefillSeconds=30;reactionCacheEnabled=$false;showBubble=$false};habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;wakeOnAction=$true;maxObjects=8};'deepseek-pricing'=@{enabled=$true;mode='force-normal';fixtureOrigin=$fixture}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath (Join-Path $root config.json) -Encoding utf8;Start-Backend;$null=Api '/api/model' @{id='actor'}
 Plan @((Call 'widget.animate' @{state='curious';seconds=1}),(Call 'widget.status' @{}),(Call 'state.observe' @{}),(Reply '三项操作都完成了。'));Message '测试改变姿态、核对姿态后完成第三项操作'
 $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;$trace=Api '/api/trace' @{id=$info.trace_id};Check (@($trace.tool_results).Count-eq3-and@($trace.tool_results|Where-Object {!$_.ok}).Count-eq0) 'three action rounds remain available before the final reply'
 Plan @((Call 'activity.enqueue' @{commands=@(@{kind='move_to';seconds=0;x=.25;y=.25},@{kind='wait';seconds=1},@{kind='wait';seconds=2})}),(Reply '稍后走一走。'));Message '安排一些本地动作'
 Await {(Api '/api/activities/status').reusable_count-ge3} 'completed actions enter persistent library' 40
 $s=Settings;$s.plugins.'deepseek-pricing'.mode='force-economy';$s.plugins.widget.autonomyIdleSeconds=5;$null=Api '/api/settings' @{settings=$s};$before=Requests
 Check ((Api '/api/dashboard').deepseek_pricing.defer_prefetch) 'economy suppresses paid prefetch'
 Await {@((Api '/api/activities/status').recent|Where-Object {$_.source-like'peak-reuse:*'-and$_.status-eq'completed'}).Count-gt0} 'peak reuses a real successful action independently of model planning delay' 20
 Check ((Requests)-eq$before) 'peak activity reuse makes no model request'
 $null=Api '/api/control' @{offline=$true};Check ((Api '/api/dashboard').offline) 'offline suspends activity';$null=Api '/api/control' @{offline=$false};Await {!(Api '/api/dashboard').offline-and!(Api '/api/dashboard').paused} 'resume';Check ((Api '/api/activities/status').enabled) 'resume retains autonomous mode'
 $count=(Api '/api/activities/status').reusable_count;Stop-Backend;Start-Backend;Check ((Api '/api/activities/status').reusable_count-eq$count) 'encrypted behavior library survives restart'
 $s=Settings;$s.plugins.'deepseek-pricing'.mode='force-normal';$s.plugins.widget.autonomyIdleSeconds=5;$null=Api '/api/settings' @{settings=$s};Plan @(@{version=1;commands=@()});$before=Requests
 Await {(Requests)-gt$before} 'normal period restores model planning' 45
 Check (!(Api '/api/dashboard').deepseek_pricing.defer_prefetch) 'normal restores planning without changing baseline switches'
 Await {!(Api '/api/activities/status').generating} 'planning result collected'
 $s=Settings;$s.plugins.widget.autonomousMode=$false;$s.contextControl=@{enabled=$true;preset='economy';historyBytes=2048};$null=Api '/api/settings' @{settings=$s}
 Plan @((Reply ('旧水果'*1000)));Message '之前聊过的一个长话题'
 Plan @((Reply '图片还没有发送。'));Message '请检查图片是不是已经发出去了。'
 Plan @((Reply '正在核对发送记录。'));Message '没有收到图片。'
 $wire=Get-Content -LiteralPath (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 90;$context=Model-CurrentEvent $wire;$texts=@($context.authorized_context|ForEach-Object {$_.data.text})
 Check ($texts-contains'请检查图片是不是已经发出去了。'-and$texts-contains'图片还没有发送。'-and$texts-contains'没有收到图片。') 'small real context keeps the immediately preceding request and reply'
 $latest=@(foreach($message in $wire.messages){$event=Model-Event $message;if($event){$event}})[-1];$fresh=@($latest.authorized_context|ForEach-Object {$_.data.text});Check (@($fresh|Where-Object {$_-like'*旧水果*'}).Count-eq0) 'older long dialogue is not resent in the latest incremental context'
 Stop-Backend;@{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Recovery flow passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}};if($desktop){$desktop.Dispose()}}
