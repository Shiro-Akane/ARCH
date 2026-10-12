export type Workspace = 'config' | 'plotfile' | 'cellular' | 'mock';
/** Archived samples are opt-in debugging routes, not production workspaces. */
export function initialWorkspace(search = ''): Workspace {
  const demo = new URLSearchParams(search).get('demo');
  return demo === 'hotspot' ? 'mock' : demo === 'cellular' ? 'cellular' : 'config';
}
export const workspaceOptions = [
  { value: 'config', label: 'Config' },
  { value: 'plotfile', label: 'Plotfile' },
] as const;
