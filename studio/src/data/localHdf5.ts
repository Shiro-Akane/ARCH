import h5wasm from 'h5wasm';
let nextFile = 0;
export async function withLocalHdf5<T>(file: File, read: (handle: InstanceType<typeof h5wasm.File>) => T): Promise<T> {
  if (file.size === 0 || file.size > 16 * 1024 * 1024) throw new Error('Choose a non-empty local HDF5 file of at most 16 MiB.');
  const bytes = new Uint8Array(await file.arrayBuffer());
  const { FS } = await h5wasm.ready;
  const path = `/plotfile-${++nextFile}.h5`;
  FS.writeFile(path, bytes);
  let handle: InstanceType<typeof h5wasm.File> | undefined;
  try {
    try { handle = new h5wasm.File(path, 'r'); if (handle.file_id < 0n) throw new Error('Invalid HDF5 handle'); }
    catch { throw new Error('Could not open HDF5 file. Choose a valid, fully written ARCH plotfile.'); }
    return read(handle);
  } finally {
    try { if (handle && handle.file_id >= 0n) handle.close(); }
    finally { FS.unlink(path); }
  }
}
