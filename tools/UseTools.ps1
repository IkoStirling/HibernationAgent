# Dot-source this file to enable the bundled tools in this PowerShell session.
$haToolsBin = Join-Path $PSScriptRoot 'bin'
if (!(Test-Path -LiteralPath (Join-Path $haToolsBin git.cmd))) { throw 'Bundled tool wrappers are missing.' }
$env:PATH = "$haToolsBin;$env:PATH"
Write-Output 'Bundled Git and 7-Zip enabled for this process only.'
