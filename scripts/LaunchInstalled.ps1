param([ValidateSet('Start','Stop','Restart','Dashboard')][string]$Action='Start')
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath($PSScriptRoot);. (Join-Path $root PackageFiles.ps1);Assert-NoReparsePath (Join-Path $root current.json);$pointer=Get-Content -LiteralPath (Join-Path $root current.json) -Raw|ConvertFrom-Json
if($pointer.schema_version-ne1-or$pointer.current-notmatch'^[A-Za-z0-9][A-Za-z0-9._-]{1,100}$'){throw 'Invalid installation pointer'}
$versions=[IO.Path]::GetFullPath((Join-Path $root versions))+[IO.Path]::DirectorySeparatorChar;$version=[IO.Path]::GetFullPath((Join-Path $versions $pointer.current));if(!$version.StartsWith($versions,[StringComparison]::OrdinalIgnoreCase)){throw 'Version path escaped installation'}
$manifestFile=Join-Path $version package-manifest.json;Assert-NoReparsePath $manifestFile;if(!$pointer.manifest_sha256-or(Get-FileHash -LiteralPath $manifestFile).Hash-ne$pointer.manifest_sha256){throw 'Installed manifest integrity check failed'};$null=Test-PackageFiles $version
$hostFile=Join-Path $version app/hibernation-agent.exe;Assert-NoReparsePath $hostFile;if((Get-FileHash -LiteralPath $hostFile).Hash-ne$pointer.host_sha256){throw 'Application integrity check failed'}
$config=Join-Path $root config/local.json
Assert-NoReparsePath $config
if($Action-eq'Stop'){& (Join-Path $version scripts/Stop.ps1) -ConfigPath $config}elseif($Action-eq'Restart'){& (Join-Path $version scripts/Restart.ps1) -ConfigPath $config -BuildRoot (Join-Path $version app)}else{& (Join-Path $version scripts/Start.ps1) -BuildRoot (Join-Path $version app) -ConfigPath $config -Dashboard:($Action-eq'Dashboard')}
