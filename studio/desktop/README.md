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
