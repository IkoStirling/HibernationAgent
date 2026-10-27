param([string]$BuildRoot=(Join-Path $PSScriptRoot '../out/build'),[string]$EngineRoot=(Join-Path $PSScriptRoot '../../AliyatEngine'),[string]$VcVars='E:/MySoftware/vs2026insider/VC/Auxiliary/Build/vcvars64.bat',[ValidateSet('All','Configure','Build','Test')][string]$Action='All',[string[]]$Targets=@('hibernation-agent','Hibernation_Checks'),[switch]$CoreOnly,[ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$EngineRoot=[IO.Path]::GetFullPath($EngineRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
New-Item -ItemType Directory -Path "$BuildRoot/tmp" -Force|Out-Null;$env:TEMP="$BuildRoot/tmp";$env:TMP=$env:TEMP
if(Test-Path -LiteralPath $VcVars){$envLines=& cmd.exe /d /c ('"'+$VcVars+'" >nul && set');if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'};foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}};$env:TEMP="$BuildRoot/tmp";$env:TMP=$env:TEMP}
$cmake='D:/Software/Cmake/bin/cmake.exe';if(!(Test-Path -LiteralPath $cmake)){$cmake='cmake'}
if($Action -in @('All','Configure')){
 $argsList=@('-S',$projectRoot,'-B',$BuildRoot,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration","-DALIYAT_ENGINE_ROOT=$EngineRoot",'-DBUILD_TESTING=ON','-DAYMATH_SIMD_LEVEL=AVX2','-DVCPKG_MANIFEST_INSTALL=OFF',"-DVCPKG_INSTALLED_DIR=$EngineRoot/out/build/t01-deps",'-DCMAKE_TOOLCHAIN_FILE=E:/MySoftware/vs2026insider/VC/vcpkg/scripts/buildsystems/vcpkg.cmake','-DCMAKE_MAKE_PROGRAM=E:/MySoftware/vs2026insider/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')
 $pluginChoice=if($CoreOnly){'OFF'}else{'ON'};$argsList+=('-DHA_BUILD_PLUGINS='+$pluginChoice)
 & $cmake @argsList;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
if($Action -in @('All','Build','Test')){
 if($Action-ne'All'-and$PSBoundParameters.ContainsKey('Configuration')){
  $cachePath=Join-Path $BuildRoot CMakeCache.txt;$entry=if(Test-Path -LiteralPath $cachePath){Select-String -LiteralPath $cachePath -Pattern '^CMAKE_BUILD_TYPE:STRING=(.+)$'|Select-Object -First 1}
  if(!$entry-or$entry.Matches[0].Groups[1].Value-ne$Configuration){throw '构建配置与目录不一致，请先用相同 Configuration 执行 -Action Configure，或选择独立 BuildRoot 执行 -Action All。'}
 }
 if($Action-eq'Test'){$Targets=@(($Targets+@('hibernation-agent','Hibernation_Checks'))|Select-Object -Unique)}
 & $cmake --build $BuildRoot --parallel 4 --target @Targets;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
if($Action -in @('All','Test')){& 'D:/Software/Cmake/bin/ctest.exe' --test-dir $BuildRoot --output-on-failure -R '^(Hibernation_UnitTests|Hibernation_Skills|Hibernation_RuntimeTools|Hibernation_Mentor|Hibernation_NapcatTests|Hibernation_ProcessIntegration|Hibernation_ConfigurationCheck|Hibernation_StateSeeding|Hibernation_ProtocolIntegration|Hibernation_Vision|Hibernation_ScreenshotRegions|Hibernation_ScheduledVision|Hibernation_TelevisionSequence|Hibernation_NativeTools|Hibernation_ContextControl|Hibernation_Pricing|Hibernation_ConversationFocus|Hibernation_PersonalThreads|Hibernation_PersonalCarry|Hibernation_PersonalityLab|Hibernation_PersonalityController|Hibernation_IntentionActivities|Hibernation_AutonomousCarry|Hibernation_Atelier|Hibernation_Reading|Hibernation_Pocket|Hibernation_AutonomousCrafting|Hibernation_ActivityReflection|Hibernation_RecoveryFlow|Hibernation_AutonomyCadence|Hibernation_ReactionCadence|Hibernation_BackgroundGeneration|Hibernation_TextReactions|Hibernation_CallBudget|Hibernation_Offline|Hibernation_AutoBalance|Hibernation_ContinuityIntegration|Hibernation_ForegroundPriority|Hibernation_MemoryActivities|Hibernation_DeskDrops|Hibernation_ChatBubbleLayout|Hibernation_ChatHistory|Hibernation_SequenceAnimation|Hibernation_AsterlanAnimation|Hibernation_EmbodiedActivities|Hibernation_LauncherCheck|Hibernation_RuntimeComposition|AYApplication_ServiceTests|AYApplication_ServiceIntegration|AYStorage_Test|AYInference_Tests|AYPluginTest|AYCrypto_Tests|AYVideoDecode_Test|AY2D_FrameSequenceTests|AYAudio_PanTests|AYAudioPlayback_Tests|AYAudioOutput_Tests|AYVideoPlayback_Tests|AYNetwork_HttpTests|AYPlatform_Test|AYPlatform_WebViewTests)$';if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}
Write-Output "完成：$BuildRoot/hibernation-agent.exe"

