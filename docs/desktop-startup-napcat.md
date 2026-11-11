# Windows login startup and NapCat runtime

Dashboard → 界面 → 开机自启动与 QQ, or the tray menu, provides an explicit current-user Windows login startup switch. It is off by default. This registers only this installation and configuration in HKCU Run. Login startup uses `--quick-start --config <file>`: no console, splash or conversation window; the tray and enabled character plugins remain available. A duplicate launch leaves the existing instance and its IPC credential unchanged. Moving the installation requires disabling the old entry and enabling it from the new location.

NapCat is an optional runtime component. Configure the directory containing NapCatWinBootMain.exe, NapCatWinBootHook.dll and napcat.mjs. QQ.exe may be specified explicitly or discovered in a parent directory. Relative paths resolve against the app configuration file. The local WebUI origin defaults to the port in NapCat's existing config/webui.json. Only loopback HTTP origins are accepted.

“随小爱一同启动 NapCat” is independent of Windows login startup. Save it with the dashboard settings. The tray and dashboard can also start/stop manually. A separate helper launches the native bootstrap without a console using the engine's owned process facility, and stops only its own process tree. An externally started instance is observed but never terminated. Closing the QR window hides the window; it does not stop NapCat. Ordinary XiaoAi exit stops the instance it owns; the backend-only restart protocol below preserves it.

The dedicated login window displays a locally rendered QR code, login state, refresh and QQ login link. The dashboard links to the full NapCat administration UI. No QQ password is collected. The WebUI token stays in NapCat's existing configuration; it is not returned to the dashboard or added to command lines or logs. WebUI 2FA requires using the full NapCat administration page; the embedded QR integration cannot bypass it.

## Backend-only restart (0.9.4)

Use `scripts/Restart.ps1` for subsequent rebuild/restart work. Do not compose a plain Stop/Start pair when QQ continuity is required.

```powershell
pwsh -NoProfile -File D:/Aliyat/HibernationAgent/scripts/Restart.ps1
# Save/stop the backend, rebuild the selected directory, then restart:
pwsh -NoProfile -File D:/Aliyat/HibernationAgent/scripts/Restart.ps1 -Rebuild -Configuration Release
```

`-BuildRoot`, `-ConfigPath` and `-NoBrowser` select another verified instance. Installed directory packages expose `Restart.cmd` / `LaunchInstalled.ps1 -Action Restart`. A package intentionally contains no compiler; its Restart action does not rebuild.

NapCat and its QQ login remain alive, while XiaoAi cannot generate replies during the backend stop/build interval. OneBot callbacks can be temporarily unavailable; upstream retry/delivery policy still applies. Process continuity is not a claim of lossless message delivery across an arbitrarily long rebuild.

The script serializes restarts, waits for current conversation work, arms one-exit preservation, saves/stops XiaoAi, optionally rebuilds, starts it again and checks health plus the exact NapCat host PID/start generation. It never restarts NapCat to conceal a failed check. Memory, keys and budgets are not replaced. `runtime/restart-last.json` retains sanitized steps/results. Build/start failure leaves NapCat available for recovery; retry after correcting the error. No force-kill or automatic archive rollback occurs. An old binary without this restart protocol is rejected before shutdown; the initial 0.9.3→0.9.4 upgrade needs the normal reviewed deployment once.

NapCat is now owned by `hibernation-napcat-host.exe`, a separate GUI-subsystem helper that reuses AYPlatform ManagedProcess. The backend uses AYPlatform detached application launch for this helper. Its current-user/SYSTEM-only named controls and single-instance mutex carry no API/WebUI credentials. The helper receives only a config path; bounded child output remains in memory and is never copied into status or logs. A content-addressed runtime copy of the helper and its declared DLL dependencies permits rebuilding/replacing program files while NapCat remains online. Existing external NapCat is observed without adoption or termination. After restart, XiaoAi reconnects to its existing helper even when autoStart=false; ordinary Save/Exit still stops its own helper/tree. A backend crash can leave the helper alive deliberately; the next launch can reconnect, or the operator can explicitly stop it. No invisible automatic restart loop is added. Retired helper copies are retained rather than deleting a possibly active version.

## Engine installation adapter boundary

`include/Hibernation/ComponentInstaller.h` defines an application-side provider boundary, not a new public engine API. DesktopIntegration can receive a provider and exposes sanitized installation status. Operator-only `component_install` accepts install/update/repair/remove plus a bounded component ID/version; it accepts no paths, URLs, shell commands or embedded scripts. The provider resolves trusted release sources and returns an asynchronous accepted/job receipt. It must keep installation files separate from memory, keys, workspaces and QQ login data, and must not claim completion before actual verification.

The 0.9.4 boundary is implemented by EngineComponentInstaller in 0.10.0. The engine is available with zero authorized sources when components.json is absent; this does not imply any component is installed. Existing local import continues to work. Downloading, trust, transactions and recovery remain engine-owned. See [engine installation](engine-installation.md) for deployment and catalog configuration. Installation is not a character tool and no NapCat source is silently authorized.

Migration should install a pinned official NapCat release archive, verify its checksum and lay it out as an optional component. Building NapCat from source on end-user computers is unnecessary. Keep source metadata/install recipes in Git and runtime binaries/login data separate. The current implementation imports an existing installation; automatic download, archive import and upgrade/rollback remain future component-manager work. Reuse portable archive tools already packaged with XiaoAi when implementing it.

Verification: DesktopIntegration tests use a synthetic local service and an isolated registry key, never the actual Windows autorun key or a QQ account. DesktopQuickStart uses a separate archive on an inactive desktop, no real model or private memory. Existing configuration, process and chat layout regressions must pass before deployment.
