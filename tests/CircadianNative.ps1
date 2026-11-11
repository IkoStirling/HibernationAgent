param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[string]$BuildRoot='',[string]$EvidenceRoot='')
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);if(!$BuildRoot){$BuildRoot=Join-Path $ProjectRoot out/continuity-8h-validation}
if(!$EvidenceRoot){$EvidenceRoot=Join-Path $ProjectRoot ('artifacts/circadian-native-'+[Guid]::NewGuid().ToString('N'))};$EvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(!$EvidenceRoot.StartsWith(([IO.Path]::GetFullPath((Join-Path $ProjectRoot artifacts))+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Dedicated evidence directory required'}
New-Item -ItemType Directory -Force $EvidenceRoot|Out-Null;Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$env:HIBERNATION_SESSION_KEY='circadian-native-fixture';$desktop=$null;$service=$null;$checks=0;$started=[DateTime]::UtcNow
$pickupSamples=[Collections.Generic.List[object]]::new();$pickupGroups=[Collections.Generic.List[object]]::new()
# 120 ms pickup design + eleven 16 ms owner-loop opportunities (~296 ms).
# Count actual response completion time, so a delayed observation cannot pass.
$pickupBudgetMs=300
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=25){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if($service.HasExited){throw 'Native backend exited'};if(& $Probe){return};Start-Sleep -Milliseconds 80};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$args=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer circadian-native-fixture'};TimeoutSec=10};if($null-ne$Data){$args.Method='Post';$args.ContentType='application/json';$args.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 40 -Compress))};Invoke-RestMethod @args}
function Plugin($Name,$Arguments){$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments};Wait-For {$script:receipt=Api '/api/plugin/result';$script:receipt.state-in@('succeeded','failed')} $Name;if(!$script:receipt.result.ok){throw ($Name+': '+($script:receipt.result|ConvertTo-Json -Compress))};return $script:receipt.result.result}
function Widget{(Api '/api/capabilities').plugins.Where({$_.id-eq'widget'})[0].current}
function Observe-Pickup([string]$Mode,[bool]$ExpectLift){
 $timer=[Diagnostics.Stopwatch]::StartNew();$group=@{mode=$Mode;started_utc=[DateTime]::UtcNow.ToString('o');budget_ms=$pickupBudgetMs;mouse_down_after_ms=$null;mouse_move_after_ms=$null;first_dragging_ms=$null;first_positive_lift_ms=$null;first_lift=$null;sample_count=0};$pickupGroups.Add($group)
 $desktop.Mouse($window,0x201,240,340);$group.mouse_down_after_ms=$timer.Elapsed.TotalMilliseconds;$desktop.Mouse($window,0x200,280,360);$group.mouse_move_after_ms=$timer.Elapsed.TotalMilliseconds
 $firstDragging=$null;$positive=$null;$last=$null;$allZero=$true
 do{
  $from=$timer.Elapsed.TotalMilliseconds;$body=Widget;$to=$timer.Elapsed.TotalMilliseconds
  $sample=@{mode=$Mode;observed_from_ms=$from;observed_to_ms=$to;dragging=[bool]$body.dragging;pointer_pressed=[bool]$body.pointer_pressed;lift=$body.drag_lift_pixels;reduced_motion=[bool]$body.reduced_motion;circadian_sleeping=[bool]$body.circadian_sleeping;state=$body.state};$pickupSamples.Add($sample);$group.sample_count++;$last=$sample
  if($body.dragging-and$null-eq$firstDragging){$firstDragging=$sample;$group.first_dragging_ms=$to;$group.first_lift=$body.drag_lift_pixels}
  if($body.drag_lift_pixels-ne0){$allZero=$false}
  if($body.dragging-and$body.drag_lift_pixels-gt0-and$null-eq$positive){$positive=$sample;$group.first_positive_lift_ms=$to}
  if($ExpectLift-and$null-ne$positive){break};if($timer.Elapsed.TotalMilliseconds-lt$pickupBudgetMs){Start-Sleep -Milliseconds 8}
 }while($timer.Elapsed.TotalMilliseconds-lt$pickupBudgetMs)
 @{first_dragging=$firstDragging;positive=$positive;last=$last;all_zero=$allZero;elapsed_ms=$timer.Elapsed.TotalMilliseconds;samples=$group.sample_count}
}
try{
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $EvidenceRoot data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins;$cfg.profiles=@($cfg.profiles[0]);$cfg.frontend.startupAnimation=$false;$cfg.review=@{enabled=$false};$cfg.cognition=@{enabled=$true};$cfg.solitudeIntervalMs=3600000;$cfg.idleBeforeSolitudeMs=3600000
 $cfg.plugins=@{widget=@{enabled=$true;skin='asterlan';animationMode='sequence';size=168;autonomousMode=$false;roam=$false;showBubble=$false;reducedMotion=$false;respectSystemMotion=$false};rhythm=@{enabled=$true;automaticRest=$false;circadianEnabled=$true;idleBeforeRestSeconds=5}}
 $config=Join-Path $EvidenceRoot config.json;$ready=Join-Path $EvidenceRoot ready.json;$cfg|ConvertTo-Json -Depth 40|Set-Content -LiteralPath $config -Encoding utf8NoBOM
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child
 Wait-For {Test-Path -LiteralPath $ready} 'native startup';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 Check (!(Api '/api/rhythm/status').configured) 'no routine is imposed before the role configures one'
 $minute=(Get-Date).Hour*60+(Get-Date).Minute;$preferences=@{enabled=$true;start_minute=($minute+1439)%1440;end_minute=($minute+120)%1440;drag_wake_probability=0;message_wake_probability=0;inertia_seconds=600}
 $saved=Plugin 'rhythm.configure' @{expected_revision=0;preferences=$preferences};Check ($saved.accepted-and$saved.circadian.preferences_revision-eq1) 'versioned role configuration accepted'
 Wait-For {(Api '/api/rhythm/status').phase-eq'sleeping'} 'actual confirmed long sleep'
 Check ((Widget).state-eq'resting'-and(Widget).circadian_sleeping) 'native body and routine agree on sleeping pose'
 Check ((Api '/api/dashboard').runtime_status.code-eq'circadian_sleep'-and!(Api '/api/dashboard').runtime_status.background_allowed) 'operator sees confirmed rest instead of an available background runner'
 $window=$desktop.Find('HibernationDinosaurOverlay');$pickup=Observe-Pickup 'normal' $true
 Check ($null-ne$pickup.first_dragging) 'owned sleeping drag observed within motion deadline'
 Check (!$pickup.first_dragging.reduced_motion) 'physical pickup fixture explicitly enables motion independent of system preference'
 Check ($null-ne$pickup.positive-and$pickup.positive.observed_to_ms-le$pickupBudgetMs) 'sleeping body still has physical pickup movement within 300ms'
 $desktop.Mouse($window,0x202,280,360);Wait-For {(Widget).state-eq'resting'-and!(Widget).dropping} 'sleeping body returns after landing'
 Check ((Api '/api/rhythm/status').phase-eq'sleeping'-and(Widget).circadian_sleeping) 'failed probabilistic drag wake preserves sleep across landing'
 $before=@((Api '/api/chat').messages).Count;$rejected=$false;try{$null=Api '/api/message' @{request_id='invalid-sleep-input';text='INVALID_SYNTHETIC_INPUT';attachments=@('missing-fixture')}}catch{$rejected=$true};Check ($rejected-and@((Api '/api/chat').messages).Count-eq$before-and(Api '/api/rhythm/status').phase-eq'sleeping') 'invalid attachment is rejected before disturbing sleep or recording a message'
 $request='sleep-input-fixture';$input=@{request_id=$request;text='SYNTHETIC_SLEEP_INPUT_RETAINED';circadian_held=$false};$null=Api '/api/message' $input
 Wait-For {!((Api '/api/chat').busy)} 'sleep input settled';$stored=(Api '/api/message' $input).request
 Check ($stored.status-eq'completed'-and$stored.input.circadian_held-and$stored.input.text-eq$input.text) 'sleeping input retained without trusting caller override'
 $raw=@((Api '/api/state').messages|Where-Object text -eq $input.text)[0];Check ($raw.delivery_state-eq'received_during_rest'-and!$raw.observed_on_receipt) 'archive distinguishes received input from model observation at that time'
 $trace=Api '/api/trace' @{id=$stored.trace_id};Check ($trace.silent-and!$trace.model_called) 'failed wake produces silence without model call'
 Check ((Api '/api/rhythm/status').phase-eq'sleeping'-and(Widget).state-eq'resting') 'conversation preparation does not accidentally wake the body'
 Check ((Api '/api/state').continuous_state.desired_activity-eq'resting') 'unheard input does not apply an awake conversation stimulus'
 $beforeRest=(Api '/api/rhythm/status').active_rest_seconds;$restartAt=[DateTime]::UtcNow;$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'sleeping process checkpoints and stops'
 $service=$null;$desktop.Dispose();$desktop=$null;Remove-Item -LiteralPath $ready;$desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child;Wait-For {Test-Path -LiteralPath $ready} 'sleep restart';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 Wait-For {(Api '/api/rhythm/status').phase-eq'sleeping'} 'reenter same rest window';$afterRest=(Api '/api/rhythm/status').active_rest_seconds;Check ($afterRest-ge$beforeRest-and$afterRest-$beforeRest-lt3-and([DateTime]::UtcNow-$restartAt).TotalSeconds-gt4) 'sleep restarts with a new pose without adding offline time'
 $null=Api '/api/control' @{paused=$true};$elapsed=(Api '/api/rhythm/status').elapsed_seconds;Start-Sleep -Milliseconds 450
 Check ([math]::Abs((Api '/api/rhythm/status').elapsed_seconds-$elapsed)-lt.001) 'pause freezes actual rest accounting'
 $null=Api '/api/rhythm/wake' @{};Check ((Api '/api/rhythm/status').phase-eq'idle') 'administrator can wake even while paused with zero ordinary wake probability'
 Check (!(Api '/api/dashboard').runtime_status.last_turn_silent) 'no-model sleep gate is not misreported as a model choice of silence'
 $null=Api '/api/control' @{paused=$false};Wait-For {!(Widget).circadian_sleeping} 'body releases sleep lease'
 $mind=Api '/api/mind/trace';Check (($mind.events|ConvertTo-Json -Depth 12)-notmatch'SYNTHETIC_SLEEP_INPUT_RETAINED|"private"\s*:|"preferences"\s*:') 'operator trace omits private dialogue and preferences'
 Check ((Api '/api/dashboard').health-eq'ok') 'native runtime remains healthy'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'graceful native shutdown'
 $service=$null;$desktop.Dispose();$desktop=$null
 # Restart against the same encrypted fixture archive, never production data.
 Remove-Item -LiteralPath $ready;$desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child;Wait-For {Test-Path -LiteralPath $ready} 'restart';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 Check ((Api '/api/rhythm/status').configured-and(Api '/api/rhythm/status').phase-eq'idle') 'preferences persist without replaying already ended sleep'
 Check ((Plugin 'rhythm.observe' @{}).circadian.preferences_revision-eq1) 'preference revision survives process restart'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'restarted process stops'
 $service=$null;$desktop.Dispose();$desktop=$null
 # A separate archive verifies reduced-motion policy without changing Windows
 # preferences or reconfiguring the original fixture's completed rest history.
 $cfg.dataRoot=Join-Path $EvidenceRoot reduced-data;$cfg.plugins.widget.reducedMotion=$true;$cfg.plugins.widget.respectSystemMotion=$false
 $config=Join-Path $EvidenceRoot reduced-config.json;$ready=Join-Path $EvidenceRoot reduced-ready.json;$cfg|ConvertTo-Json -Depth 40|Set-Content -LiteralPath $config -Encoding utf8NoBOM
 $desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),$config,$ready,$ProjectRoot);$service=$desktop.Child;Wait-For {Test-Path -LiteralPath $ready} 'reduced motion startup';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Plugin 'rhythm.configure' @{expected_revision=0;preferences=$preferences};Wait-For {(Api '/api/rhythm/status').phase-eq'sleeping'} 'reduced motion confirmed sleep'
 $window=$desktop.Find('HibernationDinosaurOverlay');$reduced=Observe-Pickup 'reduced' $false
 Check ($null-ne$reduced.first_dragging-and$reduced.first_dragging.reduced_motion-and$reduced.last.circadian_sleeping) 'reduced motion preserves owned sleeping drag interaction'
 Check ($reduced.all_zero-and$reduced.samples-ge2-and$reduced.last.observed_to_ms-ge120) 'reduced motion deliberately keeps lift zero across pickup interval'
 $desktop.Mouse($window,0x202,280,360);Wait-For {(Widget).state-eq'resting'-and!(Widget).dropping} 'reduced motion landing returns to sleep'
 Check ((Api '/api/rhythm/status').phase-eq'sleeping') 'reduced motion drag with zero wake probability preserves sleep'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'reduced motion fixture saves and stops'
 @{completed=$true;checks=$checks;started_utc=$started.ToString('o');ended_utc=[DateTime]::UtcNow.ToString('o');real_elapsed_seconds=([DateTime]::UtcNow-$started).TotalSeconds;paid_model_calls=0;private_desktop=$true}|ConvertTo-Json|Set-Content (Join-Path $EvidenceRoot acceptance.json) -Encoding utf8NoBOM
 Write-Output "Evidence: $EvidenceRoot"
}finally{@{schema_version=1;pickup_design_ms=120;observation_budget_ms=$pickupBudgetMs;groups=$pickupGroups;samples=$pickupSamples;system_preferences_changed=$false}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $EvidenceRoot motion-samples.json) -Encoding utf8NoBOM;if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};$service.Refresh();if(!$service.HasExited){Stop-Process -Id $service.Id}}};if($desktop){$desktop.Dispose()}}
