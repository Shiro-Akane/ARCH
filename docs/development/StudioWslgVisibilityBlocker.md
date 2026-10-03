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

## 2026-10-03 recovery evidence — partial native UAT

WSL boot ID changed from a1b5d54e-bd9d-44a7-b373-1d03e908b4e2 to
d8090c89-57e6-4d69-bb3c-af22da56a86c before this continuation.
This agent did not execute full shutdown in this continuation. New Weston log
reports use_gfxredir=1; no matching shared-memory EIO was found in the current log.
The existing Linux launcher was started without changing source or rebuilding.

Computer Use selected the returned ARCH Studio / msrdc window, and captured the
actual independent ARCH parameter UI, not another app or the Codex browser view.
Managed Host connected; its preview-session worker uses build-studio-cpu/bin/ARCH.
Binary SHA-256 remains e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7.
Source HEAD was 39926b04c7e02b79b1a359061c28a70396260f14, working tree clean.

Native Open Config dialog displayed, accepted simulation/Sod/Sod.par, and closed
back to Studio with the Sod path and disk in-sync state. Native Save Working Copy
As dialog displayed and saved a new, previously absent file:
studio/.local/integration/native-recovery-Sod-20261003.par.
The UI switched its parameter path to this copy. On-disk bytes were 1143 and
exactly matched the original, SHA-256
9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843.
Original config was not overwritten. Saved test data stays local and ignored.

Window visibility and these native dialog paths now have direct recovery evidence.
This is not full 3C UAT PASS: explicit reopen of the saved copy, native Configure/
Build/Run/Restart, independent terminal and close/relaunch checks remain pending.
No new simulation was launched in these recovery checks.

An observed UI wording issue remains: it labels 102 combined entries as standard
keys, while the actual binary --config-schema returns 94 standard parameters.
No binary drift was found; investigate catalog augmentation before changing the
label. This observation is not a new schema count or scientific contract.

### Follow-up: Configure/Build verified; Open Config result not yet proved

Native Configure completed with succeeded status and actual CMake stdout in the
terminal drawer. Native Build succeeded, reporting ninja: no work to do.
Build ID a1fa8f1c-8fb3-48cd-9b16-d99f50f3173d records source a9f17b97,
stable tracked inputs and the unchanged e506619f... binary. This is not a clean
rebuild proof. Existing Preview became previous/stale; explicit Update Preview
returned it to Current.

Correction of the earlier Open Config observation: closing a dialog while opening
the same file was insufficient to prove replacement. A later selection of the
distinct native-sod-workflow-99a45564-e576-4682-bcb2-d6cabd077844/Sod.par
closed the dialog but did not update the displayed file identity. Treat native
Open Config replacement/reopen as NOT PASSED pending investigation. Save As
on-disk verification and visible window evidence remain valid. No Run was started.

A local engineering-UAT fixture exists in that ignored workflow directory;
only out_dir (new absent destination) and chk_dt (0.05) differ from the original
Sod.par, with all physical parameters and tmax=0.2 retained. Input identity is
recorded locally. It remains prepared-not-run. Do not start it until the UI's
selected saved config and Host association match the fixture.

### Native config association repair verified in production

Linux desktop now has an explicit native Open project configuration IPC and
Host /api/config/open selection route. Browser file import previously had no
trusted Host association by design. Safe reads, project containment and saved
fingerprints remain Host-owned; errors retain prior selection/Working Copy.
Full 229 Studio/Host tests and static/production checks passed.

After normal close, prior launcher/Host/warm worker all exited. The new production
window selected studio/.local/UatSod.par via the native dialog and explicit Open
button; header and Host association changed, Saved/Disk in-sync and completed
inspection were visible. This establishes distinct-file selection. Return on the
long path did not establish selection; its earlier failure cannot alone prove a
Host read bug. Full native Run/Restart and cancellation matrix remain incomplete.
