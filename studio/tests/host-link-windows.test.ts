import assert from 'node:assert/strict';
import path from 'node:path';
import test from 'node:test';
import {parseLinkDependencies} from '../host/linkDependencies.ts';

const winBuild='C:\\work\\build';
const winExecutable='C:\\work\\build\\probe.exe';

test('keeps POSIX grammar and Make escape decoding',()=>{
 const text=[
  '/tmp/arch\\ build/app: /tmp/arch\\ build/a\\ b.o /tmp/arch\\ build/c\\#d.o /tmp/arch\\ build/e$$f.o /tmp/arch\\ build/g\\:h.o \\\r',
  ' /tmp/arch\\ build/i.o\r',
  '/tmp/arch\\ build/i.o:',
  '',
 ].join('\n');
 assert.deepEqual(parseLinkDependencies(text,'/tmp/arch build','/tmp/arch build/app','linux'),[
  '/tmp/arch build/a b.o','/tmp/arch build/c#d.o','/tmp/arch build/e$f.o','/tmp/arch build/g:h.o','/tmp/arch build/i.o',
 ].sort());
 assert.throws(()=>parseLinkDependencies('/tmp/build/app: /tmp/build/a$b.o\n','/tmp/build','/tmp/build/app','linux'),/Variable expansion/);
 assert.throws(()=>parseLinkDependencies('/tmp/build/app: /tmp/build/*.o\n','/tmp/build','/tmp/build/app','linux'),/wildcard|syntax/i);
});

test('uses actual Windows grammar only on the win32 seam or host',()=>{
 const nativeSample=[
  'probe.exe: \\',
  ' C:/work/build/crt2.o \\',
  ' C:\\Users\\26428\\AppData\\Local\\Temp\\ccyM76c0.o',
  '',
 ].join('\r\n');
 const expected=[
  path.win32.resolve(winBuild,'C:/work/build/crt2.o'),
  path.win32.resolve(winBuild,'C:\\Users\\26428\\AppData\\Local\\Temp\\ccyM76c0.o'),
 ].sort();
 assert.deepEqual(parseLinkDependencies(nativeSample,winBuild,winExecutable,'win32'),expected);
 if(process.platform==='win32')assert.deepEqual(parseLinkDependencies(nativeSample,winBuild,winExecutable),expected);

 const driveOnly='probe.exe: C:/work/build/crt2.o\n';
 assert.throws(()=>parseLinkDependencies(driveOnly,'/tmp/build','/tmp/build/probe.exe','linux'),/Ambiguous/);
 if(process.platform!=='win32')assert.throws(()=>parseLinkDependencies(driveOnly,'/tmp/build','/tmp/build/probe.exe'),/Ambiguous/);
});

test('preserves Windows path case while canonicalizing the drive letter',()=>{
 assert.deepEqual(parseLinkDependencies('probe.exe: c:/work/build/Crt2.o\n','c:/work/build','C:/work/build/probe.exe','win32'),[
  'C:\\work\\build\\Crt2.o',
 ]);
 assert.throws(()=>parseLinkDependencies('Probe.exe: C:\\work\\build\\a.o\n',winBuild,winExecutable,'win32'),/expected executable/);
 assert.throws(()=>parseLinkDependencies('C:/WORK/build/probe.exe: C:\\work\\build\\a.o\n',winBuild,winExecutable,'win32'),/expected executable/);
});

test('decodes Windows Make escapes but keeps literal backslashes',()=>{
 const text='probe.exe: C:\\work\\dir\\ file\\#x\\$y.o C:\\work\\a$$b.o C:\\work\\Copy\\\\File.o\n';
 assert.deepEqual(parseLinkDependencies(text,winBuild,winExecutable,'win32'),[
  'C:\\work\\a$b.o','C:\\work\\Copy\\File.o','C:\\work\\dir file#x$y.o',
 ].sort());
});

test('rejects ADS, drive-relative paths and ambiguous separators',()=>{
 assert.throws(()=>parseLinkDependencies('probe.exe: C:/work/x:y.o\n',winBuild,winExecutable,'win32'),/Ambiguous/);
 assert.throws(()=>parseLinkDependencies('probe.exe: C:x.o\n',winBuild,winExecutable,'win32'),/Drive-relative/);
 assert.throws(()=>parseLinkDependencies('probe.exe: C:\\work\\a.o: b\n',winBuild,winExecutable,'win32'),/Ambiguous/);
 assert.throws(()=>parseLinkDependencies('probe.exe:: C:\\work\\a.o\n',winBuild,winExecutable,'win32'),/Ambiguous/);
 assert.throws(()=>parseLinkDependencies('probe.exe: C\\:/work/a.o\n',winBuild,winExecutable,'win32'),/Ambiguous/);
});

test('binds Windows targets and accepts only known phony rules',()=>{
 const good='C:/work/build/probe.exe: C:\\work\\build\\a.o\nC:\\work\\build\\a.o:\n';
 assert.deepEqual(parseLinkDependencies(good,winBuild,winExecutable,'win32'),['C:\\work\\build\\a.o']);
 assert.deepEqual(parseLinkDependencies('a: C:\\work\\build\\a.o\n',winBuild,'C:\\work\\build\\a','win32'),['C:\\work\\build\\a.o']);
 assert.throws(()=>parseLinkDependencies('C:/work/build/probe.exe: C:\\work\\build\\a.o\nC:\\work\\build\\other.o:\n',winBuild,winExecutable,'win32'),/additional/);
 assert.throws(()=>parseLinkDependencies('probe.exe: C:\\work\\build\\a.o\nC:\\work\\build\\a.o: C:\\work\\build\\b.o\n',winBuild,winExecutable,'win32'),/additional/);
});

test('retains input count budget',()=>{
 const inputs=Array.from({length:20001},(_,i)=>`/tmp/build/f${i}.o`).join(' ');
 assert.throws(()=>parseLinkDependencies(`/tmp/build/app: ${inputs}\n`,'/tmp/build','/tmp/build/app','linux'),/count exceeds budget/);
});
