param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$output=Join-Path $BuildRoot ('experiments/cognitive/budget-fixture-'+[Guid]::NewGuid().ToString('N'))
$failed=$false
try{& (Join-Path $ProjectRoot scripts/RunCognitiveModelLab.ps1) -BuildRoot $BuildRoot -OutputRoot $output -Variant continuity -MaxCalls 5|Out-Null}catch{$failed=$true}
if(!$failed){throw 'Expected bounded fixture to stop'}
$run=Get-ChildItem -LiteralPath $output -Directory|Select-Object -First 1
$report=Get-Content (Join-Path $run.FullName report.json) -Raw|ConvertFrom-Json
$budget=Get-Content (Join-Path $run.FullName budget.json) -Raw|ConvertFrom-Json
if($report.live-or$report.completed-or$budget.entries.Count-ne4){throw 'Actor was forwarded without a remaining review slot'}
if(@($budget.entries|Where-Object{$_.route-eq'actor'}).Count-ne2-or@($budget.entries|Where-Object{$_.route-eq'reviewer'}).Count-ne2){throw 'Expected two fully metered actor-review pairs'}
@{completed=$true;checks=3;real_calls=0;forwarded_fixture_calls=$budget.entries.Count;unused_review_slot=1}|ConvertTo-Json
