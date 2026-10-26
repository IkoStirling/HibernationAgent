param([string]$SdkRoot=(Join-Path $PSScriptRoot '../out/deps/ffmpeg-sdk'),[string]$EngineBin=(Join-Path $PSScriptRoot '../../AliyatEngine/out/build/windows-debug-vs2026-insider/AYRuntime/AYVideo/demo'),[string]$VcVars='E:/MySoftware/vs2026insider/VC/Auxiliary/Build/vcvars64.bat')
$ErrorActionPreference='Stop';$SdkRoot=[IO.Path]::GetFullPath($SdkRoot);$EngineBin=[IO.Path]::GetFullPath($EngineBin)
New-Item -ItemType Directory -Path $SdkRoot -Force|Out-Null
$archive=Join-Path $SdkRoot 'ffmpeg-8.1.2.tar.xz';$source=Join-Path $SdkRoot 'ffmpeg-8.1.2';$expected='464BEB5E7BF0C311E68B45AE2F04E9CC2AF88851ABB4082231742A74D97B524C'
if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri 'https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz' -OutFile $archive -TimeoutSec 180}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected){throw 'FFmpeg source checksum mismatch.'}
if(!(Test-Path -LiteralPath (Join-Path $source 'libavcodec/avcodec.h'))){& tar.exe -xf $archive -C $SdkRoot 'ffmpeg-8.1.2/libavcodec' 'ffmpeg-8.1.2/libavformat' 'ffmpeg-8.1.2/libavutil' 'ffmpeg-8.1.2/libswresample' 'ffmpeg-8.1.2/COPYING.LGPLv2.1' 'ffmpeg-8.1.2/LICENSE.md';if($LASTEXITCODE){throw 'FFmpeg header extraction failed.'}}
# These are the two public compile-time CPU properties for the existing x64
# little-endian engine binaries. No decoder implementation is downloaded/run.
@('#ifndef AVUTIL_AVCONFIG_H','#define AVUTIL_AVCONFIG_H','#define AV_HAVE_BIGENDIAN 0','#define AV_HAVE_FAST_UNALIGNED 1','#endif')|Set-Content -LiteralPath (Join-Path $source 'libavutil/avconfig.h') -Encoding ascii
$envLines=& cmd.exe /d /c ('"'+$VcVars+'" >nul && set');if($LASTEXITCODE){throw 'MSVC environment failed.'};foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
New-Item -ItemType Directory -Path (Join-Path $SdkRoot 'lib'),(Join-Path $SdkRoot 'bin') -Force|Out-Null
foreach($name in @('avcodec-62','avformat-62','avutil-60','swresample-6')){
 $binary=Join-Path $EngineBin ($name+'.dll');if(!(Test-Path -LiteralPath $binary)){throw "Engine media binary missing: $name"}
 $exports=& dumpbin.exe /nologo /exports $binary;if($LASTEXITCODE){throw 'Export inspection failed.'}
 $symbols=@($exports|ForEach-Object{if($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)\s*$'){$matches[1]}});if($symbols.Count-lt10){throw 'Unexpected FFmpeg export table.'}
 $definition=Join-Path $SdkRoot ('lib/'+$name+'.def');@(('LIBRARY "'+$name+'.dll"'),'EXPORTS')+$symbols|Set-Content -LiteralPath $definition -Encoding ascii
 & lib.exe /nologo /machine:x64 ('/def:'+$definition) ('/out:'+(Join-Path $SdkRoot ('lib/'+$name+'.lib')))|Out-Null;if($LASTEXITCODE){throw 'Import library creation failed.'}
}
foreach($name in @('avcodec-62','avformat-62','avutil-60','swresample-6')){Copy-Item -LiteralPath (Join-Path $EngineBin ($name+'.dll')) -Destination (Join-Path $SdkRoot 'bin') -Force}
@{version='8.1.2';headers='https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz';sha256=$expected;binarySource=$EngineBin;note='Reuses existing engine FFmpeg DLLs; import libraries derived from their exports.'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $SdkRoot 'provenance.json') -Encoding utf8
Write-Output 'FFmpeg decode SDK prepared from verified upstream headers and existing engine binaries.'
