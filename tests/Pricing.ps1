param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('pricing-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='pricing-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(30);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer pricing-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 70 -Compress))};Invoke-RestMethod @p}
function Plan($Items,$Review=$false){$file=if($Review){'review-plan.json'}else{'plan.json'};@{nonce=[Guid]::NewGuid().ToString('N');responses=@($Items|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Native($Id,$Name,$Arguments){@{fixture_native_calls=@(@{id=$Id;name=$Name;arguments=$Arguments})}}
function Policy($Mode,$Enabled=$true){$s=(Api '/api/dashboard').settings;$s.plugins.'deepseek-pricing'.mode=$Mode;$s.plugins.'deepseek-pricing'.enabled=$Enabled;$null=Api '/api/settings' @{settings=$s}}
function Message([int]$Actions=2){
 $prefix=[Guid]::NewGuid().ToString('N');$steps=@(for($i=1;$i-le$Actions;$i++){Native ($prefix+'-state'+$i) 'ha_state_DOT_observe' @{}});$steps+=Native ($prefix+'-final') 'ha_turn_reply' @{text='保留真实状态。'};Plan $steps
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='本地价格策略验证'};Await {!(Api '/api/chat').busy} 'turn';$chat=Api '/api/chat';if($chat.call_failed){$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$detail=$job.result|ConvertFrom-Json;@{error=$chat.call_error;trace=$(if($detail.trace_id){Api '/api/trace' @{id=$detail.trace_id}}else{$null})}|ConvertTo-Json -Depth 90|Set-Content (Join-Path $root pricing-failure.json) -Encoding utf8;Write-Output ('Failure evidence: '+$root)};Check (!$chat.call_failed) 'priced turn completes';$job=@((Api '/api/state').jobs|Where-Object id -eq chat)[0];$info=$job.result|ConvertFrom-Json;Api '/api/trace' @{id=$info.trace_id}
}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $cpp=Get-Content -LiteralPath (Join-Path $ProjectRoot tests/DeepSeekPricing.cpp) -Raw
 $html=[regex]::Match($cpp,'(?s)std::string fixture\(\)\{return R"\((.*?)\)";\}').Groups[1].Value
 if(!$html){throw 'Missing fixture'};$html|Set-Content -LiteralPath (Join-Path $providerRoot pricing.html) -Encoding utf8
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.plugins=@{'deepseek-pricing'=@{enabled=$true;mode='force-normal';fixtureOrigin=$fixture}};$cfg.computer=@{enabled=$false};$cfg.contextControl=@{enabled=$true;preset='full'};$cfg.maxToolRounds=6;$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Pricing fixture';protocol='responses';enabled=$true;origin=$fixture;model='deepseek-flash';supportsToolCalls=$true;supportsJsonObject=$true},@{id='review';label='Review';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer'})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=2};$config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content -LiteralPath $config -Encoding utf8
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$config+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru;Await {if($service.HasExited){throw 'Pricing backend exited'};Test-Path -LiteralPath $ready} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
 $null=Api '/api/model' @{id='actor'};Await {(Api '/api/dashboard').deepseek_pricing.price} 'asynchronous official-page parser';$before=Api '/api/dashboard'
 Check ($before.model_budget.used-eq0) 'price refresh uses no model calls'
 Check ($before.deepseek_pricing.fixture-and$before.deepseek_pricing.currency-eq'CNY'-and$before.deepseek_pricing.fetched_ms) 'fixture quote is identified with source time and currency'
 Check ($before.deepseek_pricing.rates.peak.output-eq8-and$before.deepseek_pricing.rates.off_peak.output-eq4) 'current official-shaped table provides both rate bands'
 $normal=Message;Check ($normal.reviews.Count-eq3-and!$normal.pricing_policy.applied) 'normal mode uses original full review workflow'
 Policy 'force-economy';$peak=Message
 Check ($peak.context_control.preset-eq'economy'-and$peak.pricing_policy.applied-and$peak.pricing_policy.effective_max_tool_rounds-eq3) 'manual economy reaches the real request pipeline'
 Check ($peak.reviews.Count-eq1-and$peak.reviews_skipped.Count-eq2) 'peak strategy retains final reviewer and removes read-only review calls'
 $last=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70
 Check ($last.tools.Count-gt2) 'two actions do not consume the third peak action slot'
 $capped=Message 3;$last=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70
 Check ($capped.turns.Count-eq4-and$last.tools.Count-gt2-and$capped.turns[0].request_footprint.tool_schema_bytes-eq$capped.turns[-1].request_footprint.tool_schema_bytes) 'final stage retains stable tool declarations while the program enforces the action limit'
 $s=(Api '/api/dashboard').settings;Check ($s.maxToolRounds-eq6-and$s.contextControl.preset-eq'full'-and$s.review.maxRewrites-eq2) 'pricing overlay never overwrites baseline settings'
 Policy 'force-normal';$restored=Message;Check ($restored.reviews.Count-eq3-and$restored.pricing_policy.effective_max_tool_rounds-eq6) 'manual normal restores original limits without restart'
 Policy 'off';Check (!(Api '/api/dashboard').deepseek_pricing.applied) 'manual off disables adjustment'
 Policy 'auto' $false;Check (!(Api '/api/dashboard').deepseek_pricing.enabled) 'plugin switch disables policy and collection'
 Policy 'auto';Await {(Api '/api/dashboard').deepseek_pricing.price} 'reenabled collector'
 $pricing=(Api '/api/dashboard').deepseek_pricing;Check ($pricing.applied-eq($pricing.period-eq'peak')) 'automatic policy follows the actual current time band'
 Policy 'auto' $false;'not a price document'|Set-Content -LiteralPath (Join-Path $providerRoot pricing.html);Policy 'auto'
 Await {(Api '/api/dashboard').deepseek_pricing.last_error-eq'pricing_format_changed'} 'format change'
 $unknown=(Api '/api/dashboard').deepseek_pricing;Check ($unknown.period-eq'unknown'-and!$unknown.applied-and$unknown.effective_max_tool_rounds-eq6) 'unrecognized source stops automatic adjustment and restores baseline'
 Check ((Api '/api/dashboard').health-eq'ok') 'pricing fetch failures do not break the application'
 Write-Output "$checks pricing integration checks passed"
}finally{if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(10000)}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id -Force}};if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id -Force}}
