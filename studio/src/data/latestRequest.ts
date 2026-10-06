// A later request owns the view, even when an older read finishes last.
export function createLatestRequest() {
  let sequence = 0;
  return async function run<T>(work: () => Promise<T>, success: (value: T) => void, failure: (error: unknown) => void) {
    const request = ++sequence;
    try { const value = await work(); if (request === sequence) success(value); }
    catch (error) { if (request === sequence) failure(error); }
  };
}
export function selectedFile(files: FileList | null): File | null {
  return files?.[0] ?? null;
}
