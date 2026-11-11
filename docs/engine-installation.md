# Engine-managed deployment

## Graphical installer / launcher

Build the `Hibernation_GuiInstaller` and `Hibernation_Installer` Release targets, then run `scripts/NewGuiInstaller.ps1 -BuildRoot <build> -PackageRoot <verified-portable-package> -Python <python>`. If the package is omitted, the current runtime pointer's verified candidate package is used. The output is a directory under `E:/Aliyat/Installers`, containing `XiaoAi-Installer.exe`, its DLLs/scripts, trusted `launcher-settings.json`, and a complete `release` source. Keep the directory together; double-click the EXE, with no development server or command prompt needed.

The native Windows window provides a directory picker, installation progress, install/update/repair, file verification, cancellation, and run. Default **独立试装** creates a new offline Mock configuration and encrypted archive under `<programRoot>.userdata`, separate from immutable program versions. **更新本机的小爱** is offered only when the configured existing configuration exists; this explicitly selects its existing archive/configuration and runtime pointer. Installing without the run checkbox publishes verified files only; **运行 / 应用更新** activates the selected version. Update and repair install complete versions rather than editing the current slot. No paid-model calls or QQ messages are made by the installer itself.

The native engine downloads/validates/publishes; an independently launched `GuiActivate.ps1` relay then invokes `DeployEngineRelease.ps1 -AlreadyInstalled`, which still inspects the selected version and uses `Restart.ps1`. The relay holds a configuration-specific activation lock, records `request.json`, `activation.log` and `result.json` below `dataRoot/runtime/launcher-jobs`, and survives closure of the GUI. It does not terminate NapCat. Existing startup registration remains governed by the normal restart protocol. Cancellation is available during installation, not during save/restart. Activation failure and installation success are separate; examine the job log before retrying.

The supplied local source serves only its pinned manifest and bundle on loopback while the GUI is open. Exact manifest SHA256 is checked before serving. An occupied port, missing source or mismatched hash disables installation; already installed versions can still be verified/run using retained policies. Remote distribution can configure an explicitly trusted HTTPS origin instead. This initial package is validated on the current Windows x64 machine and retains PowerShell 7/runtime prerequisites; it is not yet a clean-machine prerequisite bootstrapper, signed self-extracting EXE, resumable downloader or uninstaller.

Run `tests/GuiInstaller.ps1 -InstallerRoot <distribution> -EvidenceRoot <fresh-directory>` for actual GUI install, verify, Mock startup, launcher-close survival, update/key continuity, cancellation and rejected-source checks. It uses an inactive private desktop, never switches the user's desktop, creates only an independent synthetic store, sends no QQ messages and makes no paid API requests.

XiaoAi 0.10.0 links AYApplicationInstaller 0.1.0. Runtime installation uses the engine Service, not InstallPackage.ps1's older directory-copy implementation. The latter remains a legacy export path. Generic trust, transport, package expansion, journals and version publication stay in the engine.

## Release and deployment

1. Build a Release candidate in a separate build tree and run the relevant checks.
2. Export it with scripts/NewPortablePackage.ps1. This excludes local.json, archives and keys.
3. Use the engine's AYRuntime/AYApplication/installer/tools/make_install_release.py to produce release.json and release.bundle. Set product XiaoAi, the release version and entry app/hibernation-agent.exe. Host the files on the configured source. The printed SHA256 must be placed in trusted operator configuration separately from downloads.
4. A release configuration contains programRoot, origin, path, manifestSha256, allowedOrigins and optional allowLoopbackHttp (tests/local deployment only). Call scripts/DeployEngineRelease.ps1 -ReleaseConfig <file> -InstallerBuildRoot <candidate>. Installation completes and is inspected before the existing backend stops.
5. The script calls Restart.ps1 -NextPackageRoot to switch to the verified immutable version, changing only uiRoot/pluginRoot in the existing external configuration. Character source, archive, workspaces and keys stay in place. The runtime pointer selects the new app directory; Start/Restart follow that pointer, while Build continues to use its separate build directory.

The installation root must be dedicated and disjoint from data/config directories. The engine may retain failed stages and older versions. Do not place user content under it. An installer commit and successful runtime activation are separate outcomes; activation failure leaves the old version intact, with the original config backup path in restart-last.json. No automatic rollback of archives or silent fallback occurs. Restart preflight validates all app package files; the engine's own .manifest.json is the only extra metadata allowed, after engine inspection. An already-enabled current-user startup registration is updated to the new executable after a successful switch; disabled startup remains disabled.

Continue launching through the original source scripts/Start.ps1 or scripts/Restart.ps1, which resolve the installed runtime pointer. The portable export's internal Start.cmd is not an engine-installed launcher: Start.ps1 rejects creating config/data inside an engine-selected immutable version. The external config path must be retained.

Deployment also creates an independent `<programRoot>.launcher` directory with Start/Stop/Restart/Dashboard.cmd. This bootstrap copies the trusted installer executable and its local DLL closure before checking selected versions; it does not run an unverified installer from the selected payload. Policies are retained outside the immutable program tree in `<programRoot>.releases/<digest>`. These entries do not depend on source-repository scripts; launcher.json retains the external user configuration path. They still require PowerShell 7 and the machine's runtime prerequisites.

No-argument engine run is not suitable for XiaoAi's required --config/--ready-file startup. XiaoAi deliberately uses its authenticated Start/Restart protocol after engine inspection; optional standalone programs may use the installer's run API.

Kana speech keeps machine-specific Python/voice-module locations in dataRoot/runtime/speech/runtime.json, outside versioned program files. The deployment preflight imports an existing validated legacy tts/runtime.json when needed, without changing the selected backend or rate. Workers prefer the external file and retain legacy lookup for development builds. Voice assets themselves are not downloaded or silently replaced.

## Optional components

The compiled EngineComponentInstaller provider is wired into DesktopIntegration. An operator may create components.json next to local.json:

```json
{"schema":1,"components":[{"id":"sample","name":"Sample component","version":"1.0","programRoot":"E:/Aliyat/Applications/Components/sample","origin":"https://downloads.example.org","path":"/sample/release.json","manifestSha256":"REPLACE_WITH_SEPARATELY_TRUSTED_64_HEX_SHA256","allowedOrigins":["https://downloads.example.org"]}]}
```

This is trusted local configuration, not model-generated content. Load occurs at backend startup. Each root must be absolute, unique and disjoint from application data/config. The dashboard displays install/update/repair and bounded progress/result metadata. All three install a new full immutable version; no in-place replacement. Jobs run on a joined worker. Shutdown cancels package transfer cooperatively, but an ongoing manifest fetch may take its engine-bounded 30 seconds. Sources, headers and local paths are not returned as model tools. The model cannot install components. Missing catalog means an available engine with zero authorized sources, not a claim of installed components.

Installing a component does not yet auto-switch NapCat's directory or move QQ login data: choose the verified version's correct NapCat runtime directory explicitly. Removal, component dependency resolution, resume, deltas, signing/rotation and automatic downgrade prevention are not provided by engine 0.1.0 and are not simulated here. Pin authorization uses the exact manifest hash; package hashes alone are not publisher authentication.
