# Linux desktop visibility blocker: WSLg shared-memory transport

2026-10-02. Runtime environment diagnosis, not desktop UAT PASS.

## Local evidence

- WSL 2.7.14.0, kernel 6.18.33.2-2, WSLg 1.0.73.2.
- User observed taskbar icon only with default Electron and software X11 modes.
- Electron diagnostic: visible=true, minimized=false, bounds 560/250/1440/940 inside work area 2560/1440. Temporary diagnostic source changes fully reverted.
- WSLg system /mnt/shared_memory is mounted as virtiofs.
- Weston log: rdp_allocate_shared_memory failed with Input/output error; use_gfxredir = 0.
- Target-window capture returns pixels from other applications; no inputs sent to those applications.
- Controlled compositor-only restart replaced Weston PID16 with2236; user still observed taskbar icon only.
- Current default launcher and managed Host were closed normally after diagnosis; three saved ARCH Runs are succeeded. Only ARCH-Ubuntu-24.04 was listed as running.

## Upstream diagnosis and confidence

[Microsoft openvmm issue4274](https://github.com/microsoft/openvmm/issues/4274) describes a SectionFs root-inode reuse defect: the first create after FUSE teardown is rejected with EIO, disabling WSLg gfxredir. Its stated symptom and local error match. [WSLg issue1456](https://github.com/microsoft/wslg/issues/1456) reports taskbar-only invisible windows on WSLg1.0.73.

This is strong matching evidence, not an independently instrumented FUSE trace of this machine. Do not claim the precise kernel/device-host cause fully proved or that a fix is installed.

## Prepared recovery and impact

1. Obtain user authorization for complete WSL VM shutdown; compositor-only authorization does not cover terminating all distro processes.
2. Run wsl --shutdown, verify stopped state, then start ARCH-Ubuntu-24.04 and original Linux launcher.
3. Recheck Weston shared-memory/gfxredir logs and user-visible independent window before native UAT.
4. No global tmpfs overlay, WSL package downgrade/update or security-setting workaround is applied.

Full shutdown terminates remaining Linux shell/services/background processes in the running distro. User work outside the recorded ARCH jobs has not been independently identified as disposable. Project files/raw validation data persist on disk.

Until recovery succeeds, 3C native desktop/file-dialog UAT remains incomplete. Host/CLI regression and scientific results retain their documented scope. Do not substitute Codex sidebar browser rendering for native desktop acceptance.
