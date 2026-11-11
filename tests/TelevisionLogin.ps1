param([string]$Executable,[string]$ProjectRoot,[string]$ScratchRoot)
$ErrorActionPreference='Stop'
Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ('television-login-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root|Out-Null
$desktop=$null
try{
 $desktop=[PrivateDesktop]::new($Executable,(Join-Path $root unused.json),(Join-Path $root ready.json),$ProjectRoot,$false,('"'+$root+'"'))
 if(!$desktop.Child.WaitForExit(90000)){Stop-Process -Id $desktop.Child.Id;throw 'Private login fixture timed out'}
 $report=Join-Path $root result.txt
 if(Test-Path -LiteralPath $report){Get-Content -LiteralPath $report}
 if($desktop.Child.ExitCode-ne0){throw 'Private login fixture failed'}
}finally{if($desktop){$desktop.Dispose()}}
