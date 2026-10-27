param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('personal-carry-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='carry-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer carry-fixture-only'};TimeoutSec=10};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 65 -Compress))};Invoke-RestMethod @p}
function Plan($Responses,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Note($Id){@{id=$Id;expected_revision=0;kind='interest';topic='那只纸船';note='CARRY_PRIVATE_MARKER 想留意它绕过石头以后会去哪';stance='curious';source_ids=@('${event_id}')}}
function Reply($Notes=@()){@{version=1;kind='reply';text='那就先别替它决定。';calls=@();carry_forward=@($Notes)}}
function Native($Name,$Arguments){@{fixture_native_calls=@(@{id='opaque-final';name=$Name;arguments=$Arguments})}}
function Message{
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='我们把那只纸船先放在橱窗角落。'};Await {!(Api '/api/chat').busy} 'turn'
 $job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}
}
function Observe{
 Plan @(@{version=1;kind='call';text=$null;calls=@(@{id='self';tool='self.observe';args=@{}})},(Reply))
 $trace=Message;$script:lastObserve=$trace.personal_context
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 65
 $event=if($script:protocol-eq'responses'){($capture.input|Where-Object role -eq user|Select-Object -Last 1).content}else{$capture.messages[-1].content}
 if($event-isnot[string]){$event=$event[0].text}
 ($event|ConvertFrom-Json).tool_results[0].result
}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Backend exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0]);foreach($protocol in @('chat-completions','responses')){$cfg.profiles+=@{id=$protocol;label=$protocol;protocol=$protocol;enabled=$true;origin=$fixture;model='fixture-model';supportsToolCalls=$true}}
 $cfg.profiles+=@{id='review';label='Review';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer'}
 $cfg.plugins=@{};$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.review=@{enabled=$true;profileId='review';maxRewrites=1}
 $cfg.skills=@{enabled=$true;root=(Join-Path $ProjectRoot skills);useBuiltins=$true;grants=@()}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content -LiteralPath $configPath -Encoding utf8
 $approve=@{decision='approve';reason='ok';guidance=''};Plan @($approve) $true
 Start-Backend
 $package=@((Api '/api/skills').packages|Where-Object id -eq 'workspace-helper')[0];Check ($package.approved) 'built-in Skill scope is available for the permission check'
 foreach($protocol in @('chat-completions','responses')){
  $script:protocol=$protocol;$null=Api '/api/model' @{id=$protocol};$note=Note ('ship-'+$protocol)
  Plan @((Native ha_turn_reply @{text='那就先别替它决定。';carry_forward=@($note);appraisal=@{source_id='${event_id}';valence=.25}}));$trace=Message
  Check ($trace.carry_forward_results[0].receipt.ok) "$protocol final note passes ordinary self.notice validation"
  Check ($trace.turns.Count-eq1-and$trace.reviews.Count-eq1) "$protocol note and dialogue share one generation and semantic review"
  Check ($trace.appraisal_result.ok-and$trace.appraisal_result.result.state.target_valence-eq.25) "$protocol native final reply carries its feeling through the existing approved update path"
  $observed=Observe;Check (@($observed.threads|Where-Object id -eq $note.id).Count-eq1) "$protocol successful note is in the following private self context"
  Check (!((Api '/api/chat').messages|ConvertTo-Json -Depth 20).Contains('CARRY_PRIVATE_MARKER')) "$protocol private note is absent from displayed conversation"
  $invalid=Note ('forged-'+$protocol);$invalid.source_ids=@('not-an-authorized-source');$invalidReply=Reply @($invalid);$invalidReply.appraisal=@{source_id='not-an-authorized-source';valence=1};Plan @($invalidReply);$trace=Message
  Check (!$trace.carry_forward_results[0].receipt.ok-and$trace.carry_forward_results[0].receipt.error.code-eq'personal_source_not_available') "$protocol invented evidence cannot create a disposition"
  Check (!(Api '/api/chat').call_failed) "$protocol failed private update does not erase the approved dialogue"
  Check ($trace.appraisal_result.error.code-eq'appraisal_source_not_available') "$protocol final appraisal cannot fabricate its experience source"
  $rejected=Note ('rejected-'+$protocol);$rejectedReply=Reply @($rejected);$rejectedReply.appraisal=@{source_id='${event_id}';valence=1};Plan @($rejectedReply,(Reply));Plan @(@{decision='rewrite';reason='continuity';guidance='只保留当前意思，不接受这条私记。'},$approve) $true;$trace=Message
  Check ($trace.carry_forward_results.Count-eq0-and$trace.reviews.Count-eq2-and$null-eq$trace.appraisal_result) "$protocol rejected candidate leaves no private side effect"
  Plan @($approve) $true
  $silent=Note ('quiet-'+$protocol);Plan @((Native ha_turn_silence @{carry_forward=@($silent)}));$trace=Message
  Check ($trace.silent-and$trace.reviews.Count-eq1-and$trace.carry_forward_results[0].receipt.ok) "$protocol silent authored updates still require semantic review"
  Plan @((Native ha_turn_silence @{appraisal=@{source_id='${event_id}';valence=-.15}}));$trace=Message
  Check ($trace.silent-and$trace.reviews.Count-eq1-and$trace.appraisal_result.ok-and$trace.appraisal_result.result.state.target_valence-eq-.15) "$protocol silence with only an appraisal cannot bypass semantic review"
  $restricted=Note ('restricted-'+$protocol)
  $restrictedReply=Reply @($restricted);$restrictedReply.appraisal=@{source_id='${event_id}';valence=1};Plan @(@{version=1;kind='call';text=$null;calls=@(@{id='activate';tool='skills.activate';args=@{id='workspace-helper';digest=$package.digest}})},$restrictedReply);$trace=Message
  Check ($trace.carry_forward_results[0].receipt.error.code-eq'skill_tool_denied') "$protocol final note cannot escape an active Skill tool scope"
  Check ($trace.appraisal_result.error.code-eq'skill_tool_denied') "$protocol final appraisal cannot escape an active Skill tool scope"
 }
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'clean shutdown'
 Start-Backend;$script:protocol='chat-completions';$null=Api '/api/model' @{id=$protocol};$observed=Observe
 Check ($observed.threads.Count-eq4) 'only approved sourced notes survive restart; rejected and restricted notes remain absent'
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'second clean shutdown'
 $bytes=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)))
 Check (!$bytes.Contains('CARRY_PRIVATE_MARKER')) 'authored disposition notes remain encrypted at rest'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json)
 Write-Output "Personal carry passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
