# Studio dependency notices

The package manifest selects direct dependencies; `package-lock.json` fixes the complete dependency tree. `DEPENDENCY-LICENSES.json` records the locked inventory and is updated with dependency changes.

| Package | Version | License | Kind |
| --- | --- | --- | --- |
| @h5web/lib | 17.0.0 | MIT | dependencies |
| h5wasm | 0.10.3 | NIST; bundled HDF5 notice | dependencies |
| ndarray | 1.1.1 | MIT | dependencies |
| react | 18.3.1 | MIT | dependencies |
| react-dom | 18.3.1 | MIT | dependencies |
| @electron/packager | 20.3.0 | BSD-2-Clause | devDependencies |
| @eslint/js | 10.0.1 | MIT | devDependencies |
| @types/ndarray | 1.1.0 | MIT | devDependencies |
| @types/node | 24.13.4 | MIT | devDependencies |
| @types/react | 18.3.31 | MIT | devDependencies |
| @types/react-dom | 18.3.7 | MIT | devDependencies |
| @vitejs/plugin-react | 6.1.1 | MIT | devDependencies |
| electron | 44.9.1 | MIT | devDependencies |
| eslint | 10.10.0 | MIT | devDependencies |
| eslint-plugin-react-hooks | 7.1.1 | MIT | devDependencies |
| eslint-plugin-react-refresh | 0.5.6 | MIT | devDependencies |
| globals | 16.5.0 | MIT | devDependencies |
| typescript | 5.9.3 | Apache-2.0 | devDependencies |
| typescript-eslint | 8.70.0 | MIT | devDependencies |
| vite | 8.3.4 | MIT | devDependencies |

H5Web supplies the public visualization API. h5wasm decodes HDF5 for the browser and isolated Host worker; preserve its NIST notice and bundled HDF5 license. Its source is maintained by [NIST](https://github.com/usnistgov/h5wasm).

Electron supplies the native Linux/Windows desktop window. Packages include its `LICENSE` and `LICENSES.chromium.html`, standalone Node's `LICENSE.node`, and the h5wasm/HDF5 notices. The packager checks the Electron manifest, lockfile and installed version before assembling the desktop.

ARCH-owned code uses the root MIT license. The scientific Core and system graphical/runtime dependencies are installed separately; their notices remain with their respective distributions.
