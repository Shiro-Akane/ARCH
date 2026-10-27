/**
 * Manage the framed persistent Core Preview subprocess and ordered requests.
 * Bound frames, queue/progress and elapsed work; match response identities before
 * resolving a request. Cancellation/shutdown reap the owned child. Resource reuse
 * inside Core does not imply evolved state or scientific output.
 */
import { spawn } from 'node:child_process';
import type { ChildProcessWithoutNullStreams } from 'node:child_process';
import {spawnOwned,terminateOwned} from './hostProcess.ts';
import {windowsPreviewResourcePolicy} from '../src/host/workflowValidation.ts';
export {windowsPreviewResourcePolicy} from '../src/host/workflowValidation.ts';
import type {WindowsHostTools} from './hostProcess.ts';
import { createHash, randomUUID } from 'node:crypto';
type ObjectValue = Record<string, unknown>;
function object(value: unknown): ObjectValue {
    if (!value || typeof value !== 'object' || Array.isArray(value))
        throw new Error('Invalid session object.');
    return value as ObjectValue;
}
function boundedInteger(value: unknown, min: number, max: number): number {
    if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < min || value > max)
        throw new Error('Invalid session capability limit.');
    return value;
}
export interface SessionCapability {
    workerPlatform?: 'windows';
    resourcePolicy?: 'windows-job-commit-user-v1';
    version: '1';
    maxRequests: number;
    maxRequestBytes: number;
    maxResponseBytes: number;
    maxConfigBytes: number;
    heavyRequestWallSeconds: number;
    meshRequestWallSeconds: number;
}
export function sessionCapability(capabilities: unknown): SessionCapability | undefined {
    const root = object(capabilities);
    if (root.extensions === undefined)
        return;
    const extensions = object(root.extensions);
    if (extensions.session === undefined)
        return;
    const value = object(extensions.session);
    if (value.supported === false)
        return;
    if (value.supported !== true || value.version !== '1' || (value.workerPlatform!=='linux'&&value.workerPlatform!=='windows') ||
        value.transport !== 'ndjson' || value.maxInFlight !== 1)
        throw new Error('Unsupported Preview session contract.');
    if(value.workerPlatform==='windows'){
        if(!windowsPreviewResourcePolicy(value.resourcePolicy)||value.heavyRequestCpuSeconds!==300||value.meshRequestCpuSeconds!==30||
            value.processAddressSpaceMiB!==undefined)
            throw new Error('Unsupported native Windows Preview resource contract.');
    }
    return {
        ...(value.workerPlatform==='windows'?{workerPlatform:'windows' as const,resourcePolicy:'windows-job-commit-user-v1' as const}:{}),
        version: '1', maxRequests: boundedInteger(value.maxRequests, 1, 256),
        maxRequestBytes: boundedInteger(value.maxRequestBytes, 1, 8 * 1024 * 1024),
        maxResponseBytes: boundedInteger(value.maxResponseBytes, 1, 9 * 1024 * 1024),
        maxConfigBytes: boundedInteger(value.maxConfigBytes, 1, 1024 * 1024),
        heavyRequestWallSeconds: boundedInteger(value.heavyRequestWallSeconds, 1, 360),
        meshRequestWallSeconds: boundedInteger(value.meshRequestWallSeconds, 1, 45),
    };
}
export type SessionRequest = {
    command: '--preview' | '--inspect-config' | '--inspect-case' | '--amr-resources' | '--preview-amr';
    caseId: string;
    requestId: string;
    configText: string;
    samples?: number;
    samplesX1?: number;
    samplesX2?: number;
    samplesX3?: number;
    meshMaxBlocks?: number;
    meshMemoryMiB?: number;
};
export interface SessionProgress {
    stage: string;
    elapsedMilliseconds: number;
}
export interface SessionResult {
    response: unknown;
    exitCode: number;
    elapsedMilliseconds: number;
    stages: {
        stage: string;
        milliseconds: number;
    }[];
    resources: ObjectValue;
    processToken: string;
    sequence: number;
    transportParseMilliseconds: number;
}
export interface SessionOptions {
    windowsTools?: WindowsHostTools;
    binary: string;
    cwd: string;
    capability: SessionCapability;
    spawn?: typeof spawn;
    readyTimeoutMs?: number;
    timeoutMs?: number;
    graceMs?: number;
}
const stages = new Set(['request', 'input', 'configuration', 'support', 'setup', 'eos', 'sampling', 'initialization', 'initial-refinement', 'source-validation', 'complete']);
function duration(value: unknown): number {
    if (typeof value !== 'number' || !Number.isFinite(value) || value < 0)
        throw new Error('Invalid session duration.');
    return value;
}
interface Active {
    request: SessionRequest;
    revision: string;
    sequence: number;
    timer: ReturnType<typeof setTimeout>;
    resolve: (result: SessionResult) => void;
    reject: (error: Error) => void;
    progress?: (event: SessionProgress) => void;
}
/** One process, one in-flight envelope. Latest-pending scheduling belongs to PreviewRunner. */
export class PreviewSession {
    readonly processToken = randomUUID();
    readonly ready: Promise<void>;
    readonly closed: Promise<void>;
    private options: SessionOptions;
    private child: ChildProcessWithoutNullStreams;
    private readyResolve!: () => void;
    private readyReject!: (error: Error) => void;
    private closedResolve!: () => void;
    private readyTimer: ReturnType<typeof setTimeout>;
    private killTimer?: ReturnType<typeof setTimeout>;
    private active?: Active;
    private buffer = Buffer.alloc(0);
    private sequence = 0;
    private receivedReady = false;
    private invalid = false;
    private closeSeen = false;
    private diagnostics = Buffer.alloc(0);
    private parseMilliseconds = 0;
    constructor(options: SessionOptions) {
        if(process.platform==='win32'&&(options.capability.workerPlatform!=='windows'||options.capability.resourcePolicy!=='windows-job-commit-user-v1'))
            throw new Error('Native Windows Preview requires the approved resource contract.');
        if(process.platform!=='win32'&&options.capability.workerPlatform==='windows')
            throw new Error('Preview worker platform does not match this Host.');
        this.options = {...options,...(options.windowsTools?{windowsTools:structuredClone(options.windowsTools)}:{})};
        this.ready = new Promise((resolve, reject) => { this.readyResolve = resolve; this.readyReject = reject; });
        void this.ready.catch(() => { });
        this.closed = new Promise(resolve => { this.closedResolve = resolve; });
        const spawnOptions={
            cwd: options.cwd, shell: false, detached: true, stdio: ['pipe', 'pipe', 'pipe'],
            env: { PATH: '/usr/bin:/bin', HOME: process.env.HOME ?? '/home/arch', LANG: 'C.UTF-8', OMP_NUM_THREADS: '1', CUDA_VISIBLE_DEVICES: '' },
        } as const;
        this.child = (options.spawn?options.spawn(options.binary,['--preview-session'],{...spawnOptions,stdio:['pipe','pipe','pipe']}):
            spawnOwned(options.binary,['--preview-session'],{...spawnOptions,stdio:['pipe','pipe','pipe']},options.windowsTools)) as ChildProcessWithoutNullStreams;
        this.readyTimer = setTimeout(() => this.fail(new Error('Preview session ready timeout.')), options.readyTimeoutMs ?? 3000);
        this.child.stdout.on('data', (chunk: Buffer) => this.consume(chunk));
        this.child.stderr.on('data', (chunk: Buffer) => {
            // Drain all stderr, retain only its bounded tail; diagnostics cannot block scientific stdout.
            this.diagnostics = Buffer.concat([this.diagnostics, chunk.subarray(-65536)]).subarray(-65536);
        });
        this.child.stdin.on('error', () => this.fail(new Error('Preview session input pipe failed.')));
        this.child.once('error', () => this.fail(new Error('Preview session could not start.')));
        this.child.once('close', (code) => {
            clearTimeout(this.readyTimer);
            if (this.killTimer)
                clearTimeout(this.killTimer);
            if (!this.invalid) {
                const message = this.buffer.length ? 'Truncated Preview session frame.' :
                    this.active ? 'Preview session exited before final result.' :
                        !this.closeSeen || code !== 0 ? 'Unexpected Preview session exit.' : undefined;
                if (message)
                    this.reject(new Error(message));
            }
            this.invalid = true;
            this.closedResolve();
        });
    }
    get pid() { return this.child.pid; }
    get reusable() { return !this.invalid && !this.closeSeen && this.sequence < this.options.capability.maxRequests; }
    stderr() { return this.diagnostics.toString('utf8'); }
    async request(request: SessionRequest, progress?: (event: SessionProgress) => void): Promise<SessionResult> {
        await this.ready;
        if (!this.reusable || this.active)
            throw new Error('Preview session unavailable or busy.');
        const bytes = Buffer.from(JSON.stringify(request) + '\n', 'utf8');
        if (bytes.length - 1 > this.options.capability.maxRequestBytes ||
            !request.configText || Buffer.byteLength(request.configText) > this.options.capability.maxConfigBytes ||
            request.configText.includes('\0') || Buffer.from(request.configText).toString('utf8') !== request.configText)
            throw new Error('Preview session request exceeds text contract.');
        for (const id of [request.caseId, request.requestId])
            if (!id || Buffer.byteLength(id) > 128)
                throw new Error('Invalid session request identity.');
        const heavy = request.command === '--preview' || request.command === '--inspect-case';
        const wallMs = this.options.timeoutMs ?? 1000 * (heavy ? this.options.capability.heavyRequestWallSeconds : this.options.capability.meshRequestWallSeconds);
        this.parseMilliseconds = 0;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => this.fail(new Error('Preview session request timed out.')), wallMs);
            this.active = { request, revision: createHash('sha256').update(request.configText).digest('hex'),
                sequence: ++this.sequence, timer, resolve, reject, progress };
            this.child.stdin.write(bytes);
        });
    }
    async terminate(reason = 'Preview session cancelled.'): Promise<void> {
        this.fail(new Error(reason));
        await this.closed;
    }
    private reject(error: Error) {
        this.readyReject(error);
        if (this.active) {
            clearTimeout(this.active.timer);
            this.active.reject(error);
            this.active = undefined;
        }
    }
    private fail(error: Error) {
        if (this.invalid)
            return;
        this.invalid = true;
        clearTimeout(this.readyTimer);
        this.reject(error);
        const kill = (signal: NodeJS.Signals) => {
            if (!this.child.pid)
                return;
            terminateOwned(this.child,signal);
        };
        kill('SIGTERM');
        this.killTimer = setTimeout(() => kill('SIGKILL'), this.options.graceMs ?? 1000);
    }
    private consume(chunk: Buffer) {
        if (this.invalid)
            return;
        // Check every frame independently, including multiple frames in one stdout chunk.
        let offset = 0;
        while (offset < chunk.length && !this.invalid) {
            const newline = chunk.indexOf(10, offset);
            const end = newline < 0 ? chunk.length : newline;
            const part = chunk.subarray(offset, end);
            if (this.buffer.length + part.length > this.options.capability.maxResponseBytes) {
                this.fail(new Error('Preview session frame exceeds byte limit.'));
                return;
            }
            this.buffer = Buffer.concat([this.buffer, part]);
            if (newline < 0)
                return;
            const frame = this.buffer;
            this.buffer = Buffer.alloc(0);
            offset = newline + 1;
            const started = performance.now();
            try {
                const value = object(JSON.parse(new TextDecoder('utf8', { fatal: true }).decode(frame)));
                this.parseMilliseconds += performance.now() - started;
                this.event(value);
            }
            catch (error) {
                this.fail(error instanceof Error ? error : new Error('Invalid Preview session frame.'));
            }
        }
    }
    private event(value: ObjectValue) {
        if (value.version !== '1')
            throw new Error('Preview session version mismatch.');
        if (value.kind === 'preview-session-ready') {
            if (this.receivedReady || value.sequence !== 0 || this.active)
                throw new Error('Unexpected Preview session ready.');
            const announced = sessionCapability({ extensions: { session: value.capability } });
            if (!announced || JSON.stringify(announced) !== JSON.stringify(this.options.capability))
                throw new Error('Preview session capability changed.');
            this.receivedReady = true;
            clearTimeout(this.readyTimer);
            this.readyResolve();
            return;
        }
        if (!this.receivedReady)
            throw new Error('Preview session event before ready.');
        if (value.kind === 'preview-session-closed') {
            if (this.active || value.sequence !== this.sequence || value.reason !== 'request-limit' || this.sequence !== this.options.capability.maxRequests)
                throw new Error('Unexpected Preview session close.');
            this.closeSeen = true;
            return;
        }
        const active = this.active;
        if (!active || value.sequence !== active.sequence)
            throw new Error('Preview session sequence mismatch.');
        if (value.kind === 'preview-session-error') {
            if (typeof value.message !== 'string' || typeof value.fatal !== 'boolean')
                throw new Error('Invalid session error.');
            const error = new Error(value.message);
            if (value.fatal)
                this.fail(error);
            else {
                clearTimeout(active.timer);
                this.active = undefined;
                active.reject(error);
            }
            return;
        }
        const identity = object(value.identity);
        if (identity.requestId !== active.request.requestId || identity.caseId !== active.request.caseId ||
            identity.configRevision !== active.revision || value.command !== active.request.command)
            throw new Error('Preview session identity mismatch.');
        if (value.kind === 'preview-session-progress') {
            if (typeof value.stage !== 'string' || !stages.has(value.stage))
                throw new Error('Invalid Preview stage.');
            active.progress?.({ stage: value.stage, elapsedMilliseconds: duration(value.elapsedMilliseconds) });
            return;
        }
        if (value.kind !== 'preview-session-result')
            throw new Error('Unknown Preview session event.');
        if (!Number.isInteger(value.exitCode) || !Array.isArray(value.stages) || value.stages.length > 256)
            throw new Error('Invalid Preview session result.');
        const timings = value.stages.map(item => {
            const entry = object(item);
            if (typeof entry.stage !== 'string' || !stages.has(entry.stage))
                throw new Error('Invalid Preview stage duration.');
            return { stage: entry.stage, milliseconds: duration(entry.milliseconds) };
        });
        const resources = object(value.resources);
        if (resources.resultReused !== false)
            throw new Error('Preview session reused a field result.');
        for (const key of ['tableLoads', 'tableHits', 'retainedTables', 'fileContentMatches', 'fileHashes', 'retainedFileBytes'])
            boundedInteger(resources[key], 0, Number.MAX_SAFE_INTEGER);
        const result: SessionResult = { response: value.response, exitCode: value.exitCode as number,
            elapsedMilliseconds: duration(value.elapsedMilliseconds), stages: timings, resources,
            processToken: this.processToken, sequence: active.sequence, transportParseMilliseconds: this.parseMilliseconds };
        clearTimeout(active.timer);
        this.active = undefined;
        active.resolve(result);
    }
}
