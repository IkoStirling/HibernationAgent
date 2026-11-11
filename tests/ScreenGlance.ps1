param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$root=Join-Path $BuildRoot ('integration/screen-glance-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$env:HIBERNATION_SESSION_KEY='isolated-screen-glance';$desktop=$null;$service=$null;$provider=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name,[int]$Seconds=45){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if(& $Probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $Name"}
function Api($Path,$Body=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer isolated-screen-glance'};TimeoutSec=10};if($null-ne$Body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Body|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plan($Items){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 30|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Reply($Text){@{version=1;kind='reply';text=$Text;calls=@();presentation=@{speech=$false;bubble=$false}}}
function Start-Backend{$script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child;Await {if($service.HasExited){throw 'isolated host exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};$false} 'backend ready';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean shutdown';$desktop.Dispose();$script:desktop=$null}
function Availability{(Api '/api/capabilities').perception.autonomous_screen_observation}
try{
 $provider=Start-Process -FilePath (Join-Path $BuildRoot Hibernation_TestProvider.exe) -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru;Await {Test-Path (Join-Path $root provider/port)} 'local provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port))
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.frontend.startupAnimation=$false
 $cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.reflectOnActivities=$false;$cfg.review=@{enabled=$true;profileId='review';maxRewrites=0};$cfg.backgroundBudget=@{enabled=$false;autoEconomy=$false}
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Local vision fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-model';supportsVision=$true;supportsJsonObject=$true},@{id='review';label='Local review fixture';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer';supportsVision=$true;supportsJsonObject=$true})
 $cfg.computer=@{enabled=$true;allowScreenCapture=$true;autonomousObservationEnabled=$true;autonomousObservationIntervalSeconds=300;readRoots=@()}
 $cfg.plugins=@{widget=@{enabled=$true;skin='dinosaur';animationMode='procedural';roam=$false;autonomousMode=$true;autonomyIdleSeconds=5;autonomyRefillSeconds=600;reactionCacheEnabled=$false;showBubble=$false}}
 $cfg|ConvertTo-Json -Depth 55|Set-Content (Join-Path $root config.json) -Encoding utf8
 $thought=@{version=1;commands=@(@{kind='screen_glance';purpose='刚才那幅画让我有点好奇，想看看现在的画面。'})}
 $capture=@{version=1;kind='call';text=$null;calls=@(@{id='one-look';tool='computer.screenshot';args=@{target='application'}},@{id='second-look';tool='computer.screenshot';args=@{target='screen'}})}
 @{nonce='role-approval';responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $root provider/review-plan.json) -Encoding utf8
 Plan @((Reply '嗯。'),$thought,$capture,(Reply '我看了一眼，又把注意力收了回来。'))
 Start-Backend;$null=Api '/api/model' @{id='actor'};Check ((Availability).enabled) 'active vision profile and permissions expose optional observation'
 $null=Api '/api/message' @{request_id='synthetic-seed';text='画的颜色还有些犹豫。'};Await {!(Api '/api/chat').busy} 'initial conversation';Check (!(Api '/api/chat').call_failed) 'initial synthetic conversation accepted';$null=Api '/api/desktop/show' @{}
 Await {$script:job=@((Api '/api/state').jobs|Where-Object id -eq 'autonomy.screen-glance')[0];$job-and$job.state-eq'succeeded'} 'self-selected observation task' 65
 $info=$job.result|ConvertFrom-Json;$trace=Api '/api/trace' @{id=$info.trace_id};$trace|ConvertTo-Json -Depth 70|Set-Content (Join-Path $root trace.json) -Encoding utf8
 Check ($trace.kind-eq'solitude') 'observation is an independent background turn'
 $calls=@($trace.tool_results|Where-Object tool -eq 'computer.screenshot')
 Check ($calls.Count-eq2-and$calls[0].ok-and!$calls[1].ok-and$calls[1].error_code-eq'screen_observation_already_attempted') 'one admission captures at most one actual frame'
 Check (@($trace.visual_inputs|Where-Object {$_.attached-and$_.delivery_state-eq'acknowledged'}).Count-eq1) 'actual pixels delivered to model with source receipt'
 $wire=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 80
 $review=Get-Content (Join-Path $root provider/review-last-request.json) -Raw|ConvertFrom-Json -Depth 80
 $images=@($wire.messages|ForEach-Object {$_.content}|Where-Object type -eq image_url)
 $reviewImages=@($review.messages|ForEach-Object {$_.content}|Where-Object type -eq image_url)
 Check ($images.Count-ge1-and$reviewImages.Count-ge1-and$images[-1].image_url.url-eq$reviewImages[-1].image_url.url) 'role reviewer receives same actual frame'
 $availability=Availability;Check (!$availability.ready) 'cooldown suppresses another autonomous observation'
 $recent=@((Api '/api/activities/status').recent|Where-Object kind -eq screen_glance)[-1]
 Check ($recent.status-eq'scheduled') 'planned observation is not falsely recorded as completed sight'
 Stop-Backend;Start-Backend
 Check ((Availability).next_allowed_ms-eq$availability.next_allowed_ms-and!(Availability).ready) 'encrypted cooldown survives restart'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.computer.autonomousObservationEnabled=$false;$null=Api '/api/settings' @{settings=$settings}
 Check (!(Availability).enabled) 'dashboard toggle revokes autonomous observation immediately'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;real_paid_calls=0;user_desktop_captured=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Screen glance passed: $checks checks. $root"
}finally{foreach($p in @($service,$provider)){if($p){$p.Refresh();if(!$p.HasExited){Stop-Process -Id $p.Id}}};if($desktop){$desktop.Dispose()}}
