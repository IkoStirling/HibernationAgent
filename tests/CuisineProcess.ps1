param([string]$ProjectRoot=(Join-Path $PSScriptRoot '..'),[Parameter(Mandatory)][string]$BuildRoot,[string]$ArtifactRoot='')
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
if(!$ArtifactRoot){$ArtifactRoot=Join-Path $ProjectRoot artifacts}
$root=Join-Path ([IO.Path]::GetFullPath($ArtifactRoot)) ('cuisine-process-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root|Out-Null
Add-Type -Path (Join-Path $ProjectRoot tests/PrivateDesktop.cs)
$env:HIBERNATION_SESSION_KEY='isolated-cuisine-process';$desktop=$null;$service=$null;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Wait-For([scriptblock]$Probe,$Name,[int]$Seconds=25){$until=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow-lt$until){if(& $Probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $Name"}
function Api($Path,$Body=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer isolated-cuisine-process'};TimeoutSec=10};if($null-ne$Body){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Body|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @p}
function Plugin($Name,$Arguments=@{}){
 $id=[Guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{tool=$Name;args=$Arguments;request_id=$id}
 Wait-For {$script:receipt=Api '/api/plugin/result';$receipt.request_id-eq$id-and$receipt.state-in@('succeeded','failed')} $Name
 return $receipt.result
}
function Current($Name){$value=@((Api '/api/capabilities').plugins|Where-Object id -eq $Name)[0].current;if($Name-eq'canteen'){return $value.current};return $value}
function Start-Backend{
 $script:desktop=[PrivateDesktop]::new((Join-Path $BuildRoot hibernation-agent.exe),(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child
 Wait-For {if($service.HasExited){throw 'isolated backend exited'};if(Test-Path (Join-Path $root ready.json)){return (Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'isolated backend ready'
 $script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'clean isolated save and shutdown';$desktop.Dispose();$script:desktop=$null}
try{
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path $BuildRoot plugins
 $cfg.frontend.startupAnimation=$false;$cfg.solitudeIntervalMs=600000;$cfg.idleBeforeSolitudeMs=600000;$cfg.review=@{enabled=$false};$cfg.profiles=@(@{id='mock';label='Isolated mock';protocol='mock';enabled=$true;timeoutMs=10000;maxOutputTokens=2048})
 $cfg.plugins=@{};foreach($manifest in Get-ChildItem (Join-Path $BuildRoot plugins) -Recurse -Filter plugin.json){$id=(Get-Content $manifest.FullName -Raw|ConvertFrom-Json).id;$cfg.plugins[$id]=@{enabled=$false}}
 $cfg.plugins.widget=@{enabled=$true;roam=$false;autonomousMode=$false};$cfg.plugins.habitat=@{enabled=$true;autoSpawn=$false;resourceSpawn=$false;autoInteract=$false};$cfg.plugins.canteen=@{enabled=$true;automatic=$false;allowSupplyOrders=$true}
 $cfg|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend
 $caps=Api '/api/capabilities';$toolNames=@($caps.tools|ForEach-Object {if($_.tool){$_.tool}else{$_.name}})
 Check (($toolNames-contains'canteen.menu')-and($toolNames-contains'canteen.request_supplies')) 'new kitchen tools discoverable through application catalog'
 $menu=Plugin 'canteen.menu' @{cuisine='western';limit=2};Check ($menu.ok-and$menu.result.recipes.Count-eq2-and$menu.result.has_more) 'bounded filtered menu works through tool bridge'
 $page=Plugin 'canteen.menu' @{cuisine='western';limit=2;offset=$menu.result.next_offset};Check ($page.result.recipes[0].id-ne$menu.result.recipes[0].id) 'menu continuation advances'
 $recipe=Plugin 'canteen.recipe' @{recipe_id='redbean_bun'};Check ($recipe.ok-and$recipe.result.inputs.Count-eq2-and$recipe.result.units-eq'simulation_portions') 'full recipe preserves quantitative inputs and simulation basis'
 $plan=Plugin 'canteen.plan' @{recipe_id='redbean_bun'};Check ($plan.ok-and!$plan.result.ready-and$plan.result.steps.Count-eq3-and$plan.result.missing.beans-eq1-and$plan.result.missing.yeast-eq1) 'recursive missing-material plan is concrete'
 $before=(Plugin 'canteen.inventory').result|ConvertTo-Json -Depth 50 -Compress
 $bad=Plugin 'canteen.request_supplies' @{items=@(@{ingredient_id='beans';quantity=1},@{ingredient_id='unknown';quantity=1})}
 Check (!$bad.ok-and((Plugin 'canteen.inventory').result|ConvertTo-Json -Depth 50 -Compress)-eq$before) 'invalid late supply entry leaves authoritative inventory unchanged'
 $missing=Plugin 'canteen.cook' @{recipe_id='redbean_bun'};Check (!$missing.ok-and$missing.error.code-eq'canteen_ingredients_insufficient') 'missing raw materials cannot manufacture a meal'
 $supplied=Plugin 'canteen.request_supplies' @{items=@(@{ingredient_id='beans';quantity=1},@{ingredient_id='yeast';quantity=1})}
 Check ($supplied.ok-and$supplied.result.items.Count-eq2-and$supplied.result.source-eq'program_virtual_supply') 'bounded virtual supply explicitly attributed'
 Check ((Plugin 'canteen.plan' @{recipe_id='redbean_bun'}).result.ready) 'received raw ingredients make full chain feasible'
 $start=Plugin 'canteen.cook' @{recipe_id='redbean_bun'};Check ($start.ok-and!$start.result.completed-and$start.result.job.steps.Count-eq3) 'advanced recipe starts as a three-step pending job'
 $jobId=$start.result.job.id;Stop-Backend;Start-Backend
 Check ((Current 'canteen').job.id-eq$jobId-and(Current 'canteen').job.stage_recipe_id-eq'yeast_dough') 'process restart resumes same exact reserved stage'
 $cancel=Plugin 'canteen.cancel' @{job_id=$jobId};Check ($cancel.ok-and$cancel.result.materials_released-and$null-eq(Current 'canteen').job) 'cancel releases current stage without claiming a meal'
 $tea=Plugin 'canteen.cook' @{recipe_id='tea'};Check ($tea.ok-and!$tea.result.completed) 'legacy tea starts without becoming ready early'
 Wait-For {(Current 'canteen').ready_count-eq1} 'actual tea timer completes' 25
 $shelf=(Current 'canteen').shelf[0];Check ($shelf.recipe_id-eq'tea'-and$shelf.sensory.basis-eq'program_simulated_food_properties') 'finished meal retains explicit simulated sensory basis'
 $taken=Plugin 'canteen.take' @{batch_id=$shelf.id};Check ($taken.ok-and!$taken.result.completed) 'take initially returns pending publication'
 Wait-For {@((Current 'habitat').objects|Where-Object {$_.provenance.batch_id-eq$shelf.id}).Count-eq1} 'desktop meal publication'
 $food=@((Current 'habitat').objects|Where-Object {$_.provenance.batch_id-eq$shelf.id})[0]
 Check ($food.ingredient_id-eq'meal.tea'-and$food.rules.nutrition-eq6-and$food.collectible) 'desktop food identity and nutrition come from shared catalog'
 Check ((Current 'canteen').ready_count-eq0-and@((Current 'canteen').recent|Where-Object object_id -eq $food.id).Count-eq1) 'one actual desktop object consumes one shelf meal'
 $again=Plugin 'canteen.take' @{batch_id=$shelf.id};Check (!$again.ok) 'completed meal cannot be issued twice'
 $gather=Plugin 'habitat.gather' @{ingredient_id='meal.tea';count=1;destination='pantry'};Check ($gather.ok) 'gather accepts shared finished-food identity'
 Wait-For {@((Plugin 'canteen.inventory').result.items|Where-Object ingredient_id -eq 'meal.tea').Count-eq1} 'real desktop pickup enters shared warehouse' 35
 $stored=@((Plugin 'canteen.inventory').result.items|Where-Object ingredient_id -eq 'meal.tea')[0]
 Check (@((Current 'habitat').objects|Where-Object id -eq $food.id).Count-eq0) 'pickup removes desktop projection after inventory acceptance'
 $ate=Plugin 'canteen.eat_supply' @{item_id=$stored.item_id};Check ($ate.ok-and$ate.result.completed) 'shared warehouse meal is consumed exactly once'
 Check (!(Plugin 'canteen.eat_supply' @{item_id=$stored.item_id}).ok) 'same consumed instance cannot be eaten twice'
 Stop-Backend;Start-Backend
 Check (@((Plugin 'canteen.inventory').result.items|Where-Object item_id -eq $stored.item_id).Count-eq0) 'consumption retirement survives real process restart'
 Stop-Backend
 @{completed=$true;checks=$checks;root=$root;endedUtc=[DateTime]::UtcNow.ToString('O')}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Cuisine process passed: $checks checks. $root"
}finally{
 if($service){$service.Refresh();if(!$service.HasExited){try{$null=Api '/api/shutdown' @{};$null=$service.WaitForExit(5000)}catch{};$service.Refresh();if(!$service.HasExited-and$service.Path-eq(Join-Path $BuildRoot hibernation-agent.exe)){Stop-Process -Id $service.Id}}}
 if($desktop){$desktop.Dispose()}
}
