# ARCH Studio Linux / WSL desktop

Current development entry (preserves the invoking Linux working directory):

    bash /path/to/ARCH/studio/desktop/arch-studio --project /path/to/ARCH --binary build-studio-cpu/bin/ARCH --case SmoothAdvection --config validation/restart/inputs/uninterrupted.par

Requires installed Linux Electron dependencies, Linux Node 24+ in
~/.local/opt/node-studio/bin/node or /usr/bin/node, and a production studio/dist.
The entry uses production assets and starts its own authenticated loopback Host;
it does not require Vite or a manually chosen port. Linux file dialogs use native
project paths. Opening never configures or builds ARCH automatically.

The Host accepts the fixed local CPU Release profile (build-studio-cpu), or an
existing registered profile. A missing binary/build reports a prerequisite error.
Static configuration and registry inspection can work without Preview readiness;
this does not assert that the binary includes current source.

Run/Restart additionally require xterm. Delivered computations survive Studio
closure; their records remain under studio/.local/runs. Close only shuts down the
owned Host and its Preview/Configure work, waiting for active Build completion.

Linux packaging: npm run desktop:package. Keep the complete resulting directory
together; start its arch-studio script. Packaging output is local and not committed.
The user has confirmed the independent Linux window is visible and supplied
screenshots of its parameter interface. Earlier WSLg copy-mode capture failures
and taskbar-only reports are historical. Production-window inspection confirms
Sod density plateaus and its x_pos marker match the authoritative initialization
response. This establishes visibility and this display check; full native workflow
acceptance, including remaining active-task shutdown checks, is still incomplete.
See [native file lifecycle evidence](../../docs/development/StudioNativeFileLifecycleUat.zh-CN.md)
and [active-close coverage](../../docs/development/StudioNativeActiveCloseAttempt.zh-CN.md)
for the verified scope and remaining lifecycle checks.

## Historical Windows workflow (outside the current delivery scope)

# ARCH Studio desktop (Windows x64 + WSL2)

Run arch-studio.exe. No npm, Vite, browser or port setup is needed.
Keep the complete extracted folder together. This portable build is unsigned;
there is no installer, auto-update, global PATH modification or native Windows Core.

Prerequisites: WSL2 Linux project, Linux Node 24+, an existing approved Host-owned
Build Profile/build directory, a successful tracked Build manifest and compatible
CPU ARCH binary. Node is checked in the project owner's
~/.local/opt/node-studio/bin/node then /usr/bin/node.
The Windows Electron runtime is bundled. Missing requirements open the project
window with an error; launch never configures or compiles ARCH automatically.

Examples (quote each path containing spaces):

    arch-studio --case Sod --config simulation/Sod/Sod.par
    arch-studio --project /home/arch/projects/ARCH-phase2g-continuous-local-workflow --binary build-preview-audit/bin/ARCH
    arch-studio --source /home/arch/projects/ARCH-phase2g-continuous-local-workflow/simulation/Sod/Sod.cpp --config /home/arch/projects/ARCH-phase2g-continuous-local-workflow/simulation/Sod/Sod.par

In PowerShell use .\arch-studio.exe, or put the package folder on your own PATH.
From a WSL project root or nested directory, run bash /mnt/<drive>/<package>/arch-studio.
The WSL shim preserves PWD and WSL_DISTRO_NAME. --cwd can explicitly supply a discovery
start directory from Windows. The native project picker accepts WSL UNC paths;
Linux paths and Windows paths with spaces are mapped through wslpath.
Relative config/binary paths resolve against explicit project, otherwise launch cwd.
Source association is checked against the binary registry, not its filename.

Only approved profiles already in the Host can build. A random project cannot silently
inherit another project's build tree. Choose an existing registered project or ask
its maintainer to register a trusted profile. Concurrent desktop instances are not
supported; a second launch focuses the existing window and explains this restriction.

Save As uses a Windows native dialog, maps back to the selected WSL project, and
keeps the Host's create-new / symlink / path containment rules. Select a NEW .par
inside the project. Save retains conflict-aware atomic writes.

Closing a dirty window asks whether to keep editing. Closing terminates its Preview
session and Host; an active Build is allowed to finish with the closing window kept
visible. It never kills unrelated Node/ARCH processes. Relaunch creates a new Host.
Two ephemeral loopback-only ports are selected internally; authenticated readiness
checks prevent attachment to an unrelated service. WSL localhost forwarding failures
produce a startup error.

Developer packaging: in WSL studio/, npm ci (or ELECTRON_SKIP_BINARY_DOWNLOAD=1 npm ci
when cross-packaging only), then npm run desktop:package.
Output: .local/desktop-release/arch-studio-win32-x64.
Copy that directory to Windows. Desktop development UAT uses the same production
package, not Vite. Browser debug mode remains documented in studio/README.md.
Preserve Electron LICENSE / LICENSES.chromium.html and DEPENDENCY-LICENSES.json.
