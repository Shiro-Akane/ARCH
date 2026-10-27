/** Select the existing root test inventory by OS; never count inapplicable files as passing tests. */
import assert from 'node:assert/strict';
import {readdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import process from 'node:process';
import {fileURLToPath} from 'node:url';

// These files contain native Windows cases only. Keep shared assertions in the
// root shared/mixed files; new or unknown test files are selected by default.
export const windowsOnlyFiles = Object.freeze([
 'host-desktop-native.test.ts',
 'host-windows-build.test.ts',
 'host-windows-config.test.ts',
 'host-windows-handoff.test.ts',
 'host-windows-plotfile.test.ts',
]);

export function selectTestFiles(files, platform) {
 const inventory=[...files].sort();
 assert(inventory.length>0,'Test inventory must not be empty.');
 for(const name of windowsOnlyFiles)assert(inventory.includes(name),`Missing declared Windows test file: ${name}`);
 const notApplicable=platform==='win32'?[]:windowsOnlyFiles;
 const selected=inventory.filter(name=>!notApplicable.includes(name));
 assert(selected.length>0,'Applicable test inventory must not be empty.');
 return {selected,notApplicable:[...notApplicable]};
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
 const directory=path.dirname(fileURLToPath(import.meta.url));
 const files=readdirSync(directory).filter(name=>name.endsWith('.test.ts'));
 const {selected,notApplicable}=selectTestFiles(files,process.platform);
 if(notApplicable.length)process.stderr.write(`NOT_APPLICABLE (${process.platform}, Windows-only files): ${notApplicable.join(', ')}\n`);
 const result=spawnSync(process.execPath,['--test','--test-concurrency=1','--test-reporter=tap',...selected.map(name=>path.join(directory,name))],{stdio:'inherit'});
 if(result.error)throw result.error;
 process.exitCode=result.status??1;
}
