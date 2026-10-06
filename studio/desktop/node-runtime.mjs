import path from 'node:path';

export async function selectLinuxNode({explicit, bundled, searchPath = '', home, probe}) {
 if (explicit && (!path.isAbsolute(explicit) || explicit.includes('\0'))) {
  throw new Error('ARCH_STUDIO_NODE must name an absolute Linux Node executable.');
 }
 const candidates = explicit ? [explicit] : [
  ...(bundled ? [bundled] : []),
  ...searchPath.split(path.delimiter).filter(Boolean).map(dir => path.resolve(dir, 'node')),
  path.join(home, '.local/opt/node-studio/bin/node'), '/usr/bin/node',
 ];
 for (const candidate of new Set(candidates)) {
  try {
   const version = await probe(candidate);
   if (Number(version.match(/^v(\d+)\./)?.[1]) >= 24) return candidate;
  } catch { /* Inspect the next runtime without executing a shell. */ }
 }
 throw new Error(explicit
  ? 'ARCH_STUDIO_NODE does not provide Linux Node 24+. Check the selected executable.'
  : 'Linux Node 24+ missing. Put it on PATH or set ARCH_STUDIO_NODE to its absolute path.');
}
