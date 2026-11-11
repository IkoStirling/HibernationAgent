param(
 [string]$BuildRoot='',
 [string]$OutputRoot='E:/Aliyat/Backups/HibernationAgent/cognitive-experiments',
 [ValidateSet('normal','rest','disappearing','timeout','attention')][string]$Scenario='normal',
 [switch]$AttentionOff,
 [switch]$OpenReport
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot BuildPaths.ps1);$BuildRoot=Resolve-HibernationBuildRoot $BuildRoot ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')))
$executable=Join-Path ([IO.Path]::GetFullPath($BuildRoot)) 'experiments/cognitive/Hibernation_CognitivePrototype.exe'
if(!(Test-Path -LiteralPath $executable)){throw '请先按 docs/cognitive-prototype.md 构建实验目标。'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
$drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($OutputRoot));if($drive.AvailableFreeSpace-lt32MB){throw '实验输出盘至少需要 32 MB 可用空间。'}
$run=Join-Path $OutputRoot ($Scenario+'-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run -Force|Out-Null
$events=[Collections.Generic.List[object]]::new()
function Add-Event([string]$Type,[hashtable]$Data=@{}){$Data.type=$Type;$Data.event_id='event-'+($events.Count+1);$events.Add($Data)}
function Add-Food([string]$Id='apple',[double]$Salience=.4){Add-Event observe @{object=@{id=$Id;revision=1;x=.8;salience=$Salience;ttl_ms=60000}}}
function Add-Time([int]$Ms,[string]$Executor='automatic'){Add-Event advance @{ms=$Ms;executor=$Executor}}
Add-Event seed @{energy=$(if($Scenario-eq'rest'){.16}else{.8});hunger=$(if($Scenario-eq'rest'){.1}else{.8});x=.1}
switch($Scenario){
 'normal'{Add-Food;Add-Time 1000;Add-Event message @{text='先停一下，等会儿再吃。'};Add-Time 2000;Add-Event conversation_done;Add-Time 8000;Add-Time 1000}
 'rest'{Add-Time 2000;Add-Event pause @{value=$true};Add-Time 30000;Add-Event pause @{value=$false};Add-Time 14000}
 'disappearing'{Add-Food;Add-Time 500;Add-Event remove @{id='apple';revision=1};Add-Time 10000}
 'timeout'{Add-Food;Add-Time 11000 silent;Add-Time 10000}
 'attention'{Add-Food apple .4;Add-Food pear .5;Add-Food berry .6;Add-Time 1000}
}
$inputFile=Join-Path $run events.json;ConvertTo-Json -InputObject @($events.ToArray()) -Depth 12|Set-Content -LiteralPath $inputFile -Encoding utf8NoBOM
$arguments=@('--root',(Join-Path $run data),'--script',$inputFile);if($AttentionOff){$arguments+='--attention-off'}
$output=@(& $executable @arguments);if($LASTEXITCODE-ne0){throw '实验执行失败；不会读取或修改正式小爱的存档。'}
$output|Set-Content -LiteralPath (Join-Path $run report.jsonl) -Encoding utf8NoBOM
$records=@($output|ForEach-Object{$_|ConvertFrom-Json -Depth 60});$summary=$records[-1];$metrics=$summary.view.metrics
function Html([object]$Value){[Net.WebUtility]::HtmlEncode([string]$Value)}
$rows=@(foreach($record in $records|Where-Object{!$_.summary}){
 $view=$record.result.view;$kinds=@($record.result.events|ForEach-Object{$_.kind})-join', '
 '<tr><td>'+(Html $record.event_id)+'</td><td>'+(Html $view.now_ms)+'</td><td>'+(Html $view.focus)+'</td><td>'+(Html $view.operation.kind)+'</td><td>'+(Html $kinds)+'</td></tr>'
})-join"`n"
$html=@"
<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>小爱 · 连续行为实验</title>
<style>body{margin:40px auto;max-width:1100px;padding:0 24px;background:#f6f7f1;color:#283b33;font:16px/1.7 system-ui}h1{font-size:28px}p{color:#526b5d}.cards{display:flex;gap:16px;flex-wrap:wrap}.card{background:white;padding:18px 24px;border-radius:16px;border:1px solid #dce5d7}.card b{font-size:28px;display:block}table{width:100%;border-collapse:collapse;margin-top:30px;background:white}th,td{text-align:left;padding:12px;border-bottom:1px solid #e2e8de;overflow-wrap:anywhere}td:last-child{max-width:420px;font-size:13px}.note{padding:16px;background:#e9efdf;border-radius:12px}</style>
<h1>小爱 · 连续行为实验</h1><p>场景：$(Html $Scenario) · 注意驻留：$(!$AttentionOff) · 模拟时间与合成世界</p>
<div class="note">这是独立原型。没有调用真实模型、控制真实桌面或读取正式记忆；示例对白来自模拟适配器。加密实验存档与此页诊断报告分别保存。</div>
<div class="cards"><div class="card">目标完成<b>$($metrics.goals_completed)</b></div><div class="card">实际食用<b>$($metrics.consumed)</b></div><div class="card">中断<b>$($metrics.goals_interrupted)</b></div><div class="card">未知结果<b>$($metrics.goals_unknown)</b></div><div class="card">模拟模型调用<b>$($metrics.model_calls)</b></div></div>
<table><thead><tr><th>输入</th><th>模拟毫秒</th><th>当前焦点</th><th>正在执行</th><th>实际事件</th></tr></thead><tbody>$rows</tbody></table>
<p>accepted 只表示接收；operation_completed 才表示核对成功。无回执不会生成成功经历。报告仅用于本次合成实验诊断。</p></html>
"@
$reportFile=Join-Path $run report.html;[IO.File]::WriteAllText($reportFile,$html,[Text.UTF8Encoding]::new($false))
Write-Output ('实验完成：'+$Scenario+'；完成 '+$metrics.goals_completed+'，食用 '+$metrics.consumed+'，未知 '+$metrics.goals_unknown+'。')
Write-Output ('报告：'+$reportFile)
if($OpenReport){Start-Process -FilePath $reportFile|Out-Null}
