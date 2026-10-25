param([string]$BuildRoot=(Join-Path $PSScriptRoot '../out/build'),[string]$EngineRoot=(Join-Path $PSScriptRoot '../../AliyatEngine'),[string]$VcVars='E:/MySoftware/vs2026insider/VC/Auxiliary/Build/vcvars64.bat',[ValidateSet('All','Configure','Build','Test')][string]$Action='All',[string[]]$Targets=@('hibernation-agent','Hibernation_Checks'),[switch]$CoreOnly)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$EngineRoot=[IO.Path]::GetFullPath($EngineRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
New-Item -ItemType Directory -Path "$BuildRoot/tmp" -Force|Out-Null;$env:TEMP="$BuildRoot/tmp";$env:TMP=$env:TEMP
if(Test-Path -LiteralPath $VcVars){$envLines=& cmd.exe /d /c ('"'+$VcVars+'" >nul && set');if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'};foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}};$env:TEMP="$BuildRoot/tmp";$env:TMP=$env:TEMP}
$cmake='D:/Software/Cmake/bin/cmake.exe';if(!(Test-Path -LiteralPath $cmake)){$cmake='cmake'}
if($Action -in @('All','Configure')){
 $argsList=@('-S',$projectRoot,'-B',$BuildRoot,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',"-DALIYAT_ENGINE_ROOT=$EngineRoot",'-DBUILD_TESTING=ON','-DAYMATH_SIMD_LEVEL=AVX2','-DVCPKG_MANIFEST_INSTALL=OFF',"-DVCPKG_INSTALLED_DIR=$EngineRoot/out/build/t01-deps",'-DCMAKE_TOOLCHAIN_FILE=E:/MySoftware/vs2026insider/VC/vcpkg/scripts/buildsystems/vcpkg.cmake','-DCMAKE_MAKE_PROGRAM=E:/MySoftware/vs2026insider/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')
 $pluginChoice=if($CoreOnly){'OFF'}else{'ON'};$argsList+=('-DHA_BUILD_PLUGINS='+$pluginChoice)
 & $cmake @argsList;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
if($Action -in @('All','Build')){& $cmake --build $BuildRoot --parallel 4 --target @Targets;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}
if($Action -in @('All','Test')){& 'D:/Software/Cmake/bin/ctest.exe' --test-dir $BuildRoot --output-on-failure -R '^(Hibernation_UnitTests|Hibernation_ProcessIntegration|Hibernation_ProtocolIntegration|Hibernation_ContinuityIntegration|Hibernation_LauncherCheck|AYApplication_ServiceTests|AYApplication_ServiceIntegration|AYStorage_Test|AYInference_Tests|AYPluginTest|AYCrypto_Tests|AYVideoDecode_Test|AYAudio_PanTests|AYAudioPlayback_Tests|AYVideoPlayback_Tests|AYNetwork_HttpTests)$';if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}
Write-Output "完成：$BuildRoot/hibernation-agent.exe"
