param([string]$HeaderRoot=(Join-Path $PSScriptRoot '../out/deps/miniaudio'))
$ErrorActionPreference='Stop'
$HeaderRoot=[IO.Path]::GetFullPath($HeaderRoot);New-Item -ItemType Directory -Force $HeaderRoot|Out-Null
$header=Join-Path $HeaderRoot 'miniaudio.h';$uri='https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h';$expected='7E4F3F13C8FE66DF2080AC3DD12A89193E3C2463CB7F067C798ABD7331CD8EE6'
if(!(Test-Path -LiteralPath $header)){Invoke-WebRequest -Uri $uri -OutFile $header -TimeoutSec 60}
if((Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash-ne$expected){throw 'miniaudio header checksum mismatch.'}
@{version='0.11.23';source=$uri;sha256=$expected;license='Public domain or MIT-0 (embedded upstream notice)';note='Used by the existing AYAudio MiniaudioBackend; no second playback implementation.'}|ConvertTo-Json|Set-Content (Join-Path $HeaderRoot 'provenance.json') -Encoding utf8
Write-Output 'Verified miniaudio header prepared for AYAudioPlayback.'
