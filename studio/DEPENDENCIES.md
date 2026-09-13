# Frontend dependencies through M2

| Package | Version | License | Kind |
| --- | --- | --- | --- |
| @h5web/lib | 17.0.0 | MIT | dependencies |
| ndarray | 1.1.1 | MIT | dependencies |
| react | 18.3.1 | MIT | dependencies |
| react-dom | 18.3.1 | MIT | dependencies |
| @eslint/js | 10.0.1 | MIT | devDependencies |
| @types/ndarray | 1.1.0 | MIT | devDependencies |
| @types/node | 24.13.4 | MIT | devDependencies |
| @types/react | 18.3.31 | MIT | devDependencies |
| @types/react-dom | 18.3.7 | MIT | devDependencies |
| @vitejs/plugin-react | 6.1.1 | MIT | devDependencies |
| eslint | 10.10.0 | MIT | devDependencies |
| eslint-plugin-react-hooks | 7.1.1 | MIT | devDependencies |
| eslint-plugin-react-refresh | 0.5.6 | MIT | devDependencies |
| globals | 16.5.0 | MIT | devDependencies |
| typescript | 5.9.3 | Apache-2.0 | devDependencies |
| typescript-eslint | 8.70.0 | MIT | devDependencies |
| vite | 8.3.0 | MIT | devDependencies |

H5Web is used only through its public visualization API. Its peer/transitive WebGL dependencies are locked in package-lock.json. At the frozen Phase 0 checkpoint, no @h5web/app, h5wasm or HDF5 reader was installed; Phase 1A adds h5wasm as recorded below. Node.js 24.21.0 is isolated in the WSL user directory.

## Phase 1A addition (recorded before installation)
- h5wasm 0.10.3: browser-local read-only HDF5 decoding using the public NIST API; no server and no @h5web/app dependency.
- License: NIST notice plus bundled HDF5 license (package LICENSE.txt; not MIT). Preserve the complete notice. Official source: https://github.com/usnistgov/h5wasm .
- Needed to decode the bytes of files explicitly selected by the user; renderer remains independent.
