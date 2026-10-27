import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,readFile,readdir,rename,rm,stat,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import type {ChildProcess} from 'node:child_process';
import {windowsOutputLockKeys} from '../host/runOutput.ts';
import {spawnWindowsOwned,validateWindowsExecutable,windowsProcessIdentity,windowsSupervisorRunArgs} from '../host/windowsRun.ts';
import type {WindowsRunTools} from '../host/windowsRun.ts';
import {writeRunState} from '../host/runWorker.ts';
import {verifyRunSupervisor} from '../host/runSupervisor.ts';
import type {RunState,WindowsRunProcessIdentity} from '../src/host/runContracts.ts';

const native = process.platform === 'win32' && !!process.env.ARCH_TEST_SUPERVISOR;

function nativeTools(): WindowsRunTools {
    const supervisor = process.env.ARCH_TEST_SUPERVISOR;
    if (!supervisor) throw new Error('ARCH_TEST_SUPERVISOR is required for native Windows tests.');
    return {supervisor, path: process.env.PATH};
}
function parentIdentity(tools: WindowsRunTools): WindowsRunProcessIdentity {
    const identity = windowsProcessIdentity(tools, process.pid);
    assert.equal(identity.alive, true);
    return {version: 'windows-v1', pid: identity.pid, creationTime: identity.creationTime};
}
function startNode(script: string, args: string[], lockKeys: string[] = []): ChildProcess {
    const tools = nativeTools();
    return spawnWindowsOwned(process.execPath, [script, ...args], {
        cwd: path.dirname(script), stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true
    }, parentIdentity(tools), tools, lockKeys);
}
function startNodeWithTemp(script: string, args: string[], temp: string, lockKeys: string[] = []): ChildProcess {
    assert.deepEqual(windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P. \\Output'}]),
        windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P\\Output'}]));
    assert.deepEqual(windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P\\σ'}]),
        windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P\\ς'}]));
    const previous = {TEMP: process.env.TEMP, TMP: process.env.TMP};
    process.env.TEMP = temp;
    process.env.TMP = temp;
    try { return startNode(script, args, lockKeys); }
    finally {
        if (previous.TEMP === undefined) delete process.env.TEMP; else process.env.TEMP = previous.TEMP;
        if (previous.TMP === undefined) delete process.env.TMP; else process.env.TMP = previous.TMP;
    }
}
function collect(child: ChildProcess): Promise<{code: number | null; stderr: string}> {
    return new Promise((resolve, reject) => {
        let stderr = '';
        child.stderr?.setEncoding('utf8');
        child.stderr?.on('data', (chunk: string) => { if (stderr.length < 65536) stderr += chunk.slice(0, 65536 - stderr.length); });
        child.once('error', reject);
        child.once('close', code => resolve({code, stderr}));
        if (child.exitCode !== null) resolve({code: child.exitCode, stderr});
    });
}
function closed(child: ChildProcess): Promise<void> {
    return new Promise(resolve => child.exitCode !== null || child.signalCode !== null ? resolve() : child.once('close', () => resolve()));
}
async function waitForFile(file: string, timeout = 5000): Promise<void> {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
        try { await stat(file); return; }
        catch (e) { if ((e as NodeJS.ErrnoException).code !== 'ENOENT') throw e; }
        await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw new Error('Timed out waiting for native test marker.');
}

test('Windows output lock keys are stable, canonical and deduplicated', () => {
    const keys = windowsOutputLockKeys([
        {path: 'C:\\project\\alias', canonicalPath: 'c:\\project\\output'},
        {path: 'C:\\project\\other-alias', canonicalPath: 'c:\\project\\output'},
        {path: 'C:\\project\\second', canonicalPath: 'c:\\project\\second'}
    ]);
    assert.equal(keys.length, 2);
    assert.ok(keys.every(key => /^[a-f0-9]{64}$/.test(key)));
    const outputKey = windowsOutputLockKeys([{path: 'D:\\different-project', canonicalPath: 'c:\\project\\output'}]);
    assert.ok(keys.includes(outputKey[0]));
    assert.deepEqual(windowsOutputLockKeys([{path: 'D:\\different-project', canonicalPath: 'c:\\project\\output'}]), outputKey);
    assert.deepEqual(windowsOutputLockKeys([{path: 'D:\\alias', canonicalPath: 'C:\\Project\\Output\\'}]), outputKey);
    assert.deepEqual(windowsOutputLockKeys([{path: 'D:\\alias', canonicalPath: '\\\\?\\C:\\Project\\Output'}] ), outputKey);
    assert.deepEqual(windowsOutputLockKeys([{path: 'D:\\alias', canonicalPath: 'C:\\Project\\Output. '}]), outputKey);
    assert.deepEqual(windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P. \\Output'}]),
        windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P\\Output'}]));
    assert.deepEqual(windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P\\σ'}]),
        windowsOutputLockKeys([{path:'D:\\alias',canonicalPath:'C:\\P\\ς'}]));
    const previous = {TEMP: process.env.TEMP, TMP: process.env.TMP};
    try {
        process.env.TEMP = 'C:\\per-host-temp-a'; process.env.TMP = process.env.TEMP;
        const first = windowsOutputLockKeys([{path: 'D:\\alias', canonicalPath: 'C:\\Project\\Output'}]);
        process.env.TEMP = 'D:\\per-host-temp-b'; process.env.TMP = process.env.TEMP;
        assert.deepEqual(windowsOutputLockKeys([{path: 'D:\\alias', canonicalPath: 'C:\\Project\\Output'}]), first);
    } finally {
        if (previous.TEMP === undefined) delete process.env.TEMP; else process.env.TEMP = previous.TEMP;
        if (previous.TMP === undefined) delete process.env.TMP; else process.env.TMP = previous.TMP;
    }
});

test('Windows executable validation rejects UNC, device and relative paths', () => {
    assert.equal(validateWindowsExecutable('C:\\ARCH\\arch.exe'), 'C:\\ARCH\\arch.exe');
    for (const value of ['\\\\server\\share\\arch.exe', '\\\\?\\C:\\ARCH\\arch.exe', '\\\\.\\C:\\ARCH\\arch.exe',
        'relative\\arch.exe', 'C:arch.exe', 'C:\\ARCH\\arch.dll', ''])
        assert.throws(() => validateWindowsExecutable(value), /Absolute local Windows/);
});

test('Windows supervisor argv only accepts absolute executables and sorted valid lock keys', () => {
    const parent: WindowsRunProcessIdentity = {version: 'windows-v1', pid: 42, creationTime: '1234'};
    const args = windowsSupervisorRunArgs(parent, 'C:\\ARCH\\arch.exe', ['Case', 'C:\\run\\input.par'],
        ['b'.repeat(64), 'a'.repeat(64), 'a'.repeat(64)]);
    assert.deepEqual(args.slice(0, 8), ['run', '42', '1234', '--lock-key', 'a'.repeat(64), '--lock-key', 'b'.repeat(64), '--']);
    assert.throws(() => windowsSupervisorRunArgs(parent, 'relative.exe', [], []), /Absolute local Windows/);
    assert.throws(() => windowsSupervisorRunArgs(parent, 'C:\\ARCH\\arch.exe', [], ['A'.repeat(64)]), /Invalid Windows output lock key/);
});

test('run state replacement is complete and removes temporary files on failure', async () => {
    const directory = await mkdtemp(path.join(tmpdir(), 'arch-run-state-'));
    const state: RunState = {runId: '00000000-0000-0000-0000-000000000000', state: 'starting', workerPid: 123, startedAt: new Date().toISOString()};
    try {
        await writeRunState(directory, state);
        await writeRunState(directory, {...state, state: 'running'});
        assert.equal(JSON.parse(await readFile(path.join(directory, 'state.json'), 'utf8')).state, 'running');
        await assert.rejects(writeRunState(directory, {...state, bad: 1n} as never), /BigInt/);
        assert.deepEqual((await readdir(directory)).sort(), ['state.json']);
    } finally { await rm(directory, {recursive: true, force: true}); }
});

if(process.platform==='win32') test('Windows identity verifies PID plus creation time and rejects stale state', {skip: !native}, async () => {
    const tools = nativeTools();
    const identity = parentIdentity(tools);
    const state: RunState = {runId: '00000000-0000-0000-0000-000000000000', state: 'running', workerPid: process.pid,
        windowsWorker: identity, windowsHelper: identity, startedAt: new Date().toISOString()};
    await verifyRunSupervisor(state, tools);
    await verifyRunSupervisor({...state,state:'starting',windowsHelper:undefined},tools);
    await assert.rejects(verifyRunSupervisor({...state,windowsHelper:undefined},tools),/identity unavailable/);
    await assert.rejects(verifyRunSupervisor({...state, windowsHelper: {...identity, creationTime: identity.creationTime + '0'}}, tools),
        /identity unavailable/);
});

if(process.platform==='win32') test('native exclusive lock lease rejects a competitor and is released after helper death', {skip: !native}, async () => {
    const directory = await mkdtemp(path.join(tmpdir(), 'arch-native-lock-'));
    const script = path.join(directory, 'hold.cjs');
    const ready = path.join(directory, 'ready');
    await writeFile(script, "const fs=require('node:fs');fs.writeFileSync(process.argv[2],'ready');setTimeout(()=>{},Number(process.argv[3]));\n");
    const key = 'c'.repeat(64);
    const first = startNode(script, [ready, '5000'], [key]);
    const firstClosed = closed(first);
    try {
        await waitForFile(ready);
        const second = startNode(script, [path.join(directory, 'second'), '100'], [key]);
        const result = await collect(second);
        assert.notEqual(result.code, 0);
        assert.match(result.stderr, /reserved by another active Studio run/);
    } finally {
        first.kill();
        await firstClosed;
        try {
            const deadline = Date.now() + 5000;
            let released = false;
            while (Date.now() < deadline) {
                const probe = startNode(script, [path.join(directory, 'probe'), '10'], [key]);
                const result = await collect(probe);
                if (result.code === 0) { released = true; break; }
                assert.match(result.stderr, /reserved by another active Studio run/);
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            assert.equal(released, true, 'exclusive lock was not released after helper Job termination');
        } finally { await rm(directory, {recursive: true, force: true}); }
    }
});

if(process.platform==='win32') test('native lock namespace is shared across different Host TEMP values', {skip: !native}, async () => {
    const directory = await mkdtemp(path.join(tmpdir(), 'arch-native-temp-lock-'));
    const tempA = await mkdtemp(path.join(tmpdir(), 'arch-temp-a-'));
    const tempB = await mkdtemp(path.join(tmpdir(), 'arch-temp-b-'));
    const script = path.join(directory, 'hold.cjs');
    const ready = path.join(directory, 'ready');
    await writeFile(script, "const fs=require('node:fs');fs.writeFileSync(process.argv[2],'ready');setTimeout(()=>{},Number(process.argv[3]));\n");
    const key = 'd'.repeat(64);
    const first = startNodeWithTemp(script, [ready, '5000'], tempA, [key]);
    const firstClosed = closed(first);
    try {
        await waitForFile(ready);
        const second = startNodeWithTemp(script, [path.join(directory, 'second'), '100'], tempB, [key]);
        const result = await collect(second);
        assert.notEqual(result.code, 0);
        assert.match(result.stderr, /reserved by another active Studio run/);
        // The pinned lock directory itself rejects a rename while the lease lives.
        const local = process.env.LOCALAPPDATA;
        if (local) {
            const lockDirectory = path.join(local, 'ARCHStudioRunLocks-v1');
            const moved = lockDirectory + '-rebind-' + process.pid;
            let rebind: Error | undefined;
            try { await rename(lockDirectory, moved); }
            catch (e) { rebind = e as Error; }
            if (!rebind) {
                await rename(moved, lockDirectory).catch(() => undefined);
                assert.fail('lock directory was rebindable while a lease was held');
            }
            assert.ok(['EPERM', 'EACCES', 'EBUSY'].includes((rebind as NodeJS.ErrnoException).code ?? ''),
                'lock directory rebind failed for an unexpected reason: ' + rebind.message);
        }
    } finally {
        first.kill();
        await firstClosed;
        await rm(directory, {recursive: true, force: true});
        await rm(tempA, {recursive: true, force: true});
        await rm(tempB, {recursive: true, force: true});
    }
});

if(process.platform==='win32') test('native Job drains a descendant when the Core leader exits', {skip: !native}, async () => {
    const directory = await mkdtemp(path.join(tmpdir(), 'arch-native-job-'));
    const leader = path.join(directory, 'leader.cjs');
    const late = path.join(directory, 'late.cjs');
    const marker = path.join(directory, 'late-marker');
    await writeFile(leader, "const {spawn}=require('node:child_process');const child=spawn(process.execPath,[process.argv[2],process.argv[3]],{stdio:'ignore'});child.unref();\n");
    await writeFile(late, "const fs=require('node:fs');setTimeout(()=>fs.writeFileSync(process.argv[2],'late'),500);\n");
    try {
        const result = await collect(startNode(leader, [late, marker]));
        assert.equal(result.code, 0);
        await new Promise(resolve => setTimeout(resolve, 1000));
        await assert.rejects(stat(marker), (e: NodeJS.ErrnoException) => e.code === 'ENOENT');
    } finally { await rm(directory, {recursive: true, force: true}); }
});
