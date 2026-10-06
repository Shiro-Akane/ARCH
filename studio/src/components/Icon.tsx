type IconName = 'grid' | 'layers' | 'crosshair' | 'play' | 'save' | 'reset' | 'build' | 'monitor' | 'chevron' | 'expand';
const paths: Record<IconName, string> = {
  grid: 'M3 3h18v18H3z M3 9h18 M3 15h18 M9 3v18 M15 3v18',
  layers: 'm12 3 10 5-10 5L2 8z M2 12l10 5 10-5 M2 16l10 5 10-5',
  crosshair: 'M12 2v4 M12 18v4 M2 12h4 M18 12h4 M12 7a5 5 0 1 0 0 10 5 5 0 0 0 0-10',
  play: 'm8 4 12 8-12 8z',
  save: 'M4 3h13l4 4v14H3V3z M7 3v6h10V3 M7 21v-8h10v8',
  reset: 'M3 4v6h6 M3 10a9 9 0 1 1 2 9',
  build: 'm14 7 3 3 M3 21l10-10 M14 3a6 6 0 0 0-5 8l-6 6a3 3 0 0 0 4 4l6-6a6 6 0 0 0 8-7l-4 4-5-5 4-4z',
  monitor: 'M3 3h18v14H3z M8 21h8 M12 17v4 M6 12l4-4 4 3 4-5',
  chevron: 'm9 5 7 7-7 7',
  expand: 'M8 3H3v5 M16 3h5v5 M3 16v5h5 M21 16v5h-5',
};
export function Icon({ name, size = 16 }: { name: IconName; size?: number }) {
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d={paths[name]} /></svg>;
}
