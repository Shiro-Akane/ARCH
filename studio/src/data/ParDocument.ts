export interface ParEntry { key: string; value: string; start: number; end: number; line: number }
export interface ParDocument { raw: string; entries: ParEntry[]; rawLines: number[] }

// Offsets refer to the original text: serialization only replaces value tokens.
export function parsePar(raw: string): ParDocument {
  if (raw.includes('\0')) throw new Error('Unsupported config: binary/NUL content.');
  const entries: ParEntry[] = [], rawLines: number[] = [];
  const lines = raw.match(/[^\r\n]*(?:\r\n|\n|\r|$)/g) ?? [];
  let offset = 0;
  lines.forEach((line, index) => {
    const body = line.replace(/[\r\n]+$/, '');
    const comment = body.indexOf('#');
    const content = body.slice(0, comment < 0 ? body.length : comment);
    const equal = content.indexOf('=');
    if (equal < 0) { if (content.trim()) rawLines.push(index + 1); }
    else {
      const key = content.slice(0, equal).replace(/^[ \t]+|[ \t]+$/g, '');
      const rest = content.slice(equal + 1);
      const leading = rest.match(/^[ \t]*/)?.[0].length ?? 0;
      const value = rest.replace(/^[ \t]+|[ \t]+$/g, '');
      const start = offset + equal + 1 + leading;
      entries.push({ key, value, start, end: start + value.length, line: index + 1 });
    }
    offset += line.length;
  });
  return { raw, entries, rawLines };
}
export function effectiveEntries(doc: ParDocument): ParEntry[] {
  const last = new Map<string, ParEntry>();
  for (const entry of doc.entries) last.set(entry.key, entry);
  return [...last.values()].sort((a,b) => a.line - b.line);
}
export function serializePar(doc: ParDocument, changes: Record<string,string> = {}): string {
  let result = doc.raw;
  for (const entry of effectiveEntries(doc).reverse()) {
    if (!Object.hasOwn(changes, entry.key)) continue;
    const value = changes[entry.key];
    if (/[\r\n#\0]/.test(value) || value !== value.trim()) throw new Error('Value cannot contain comments, newlines or surrounding whitespace.');
    result = result.slice(0, entry.start) + value + result.slice(entry.end);
  }
  return result;
}
