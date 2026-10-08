/**
 * AHFL Playground API Middleware
 *
 * Spawns the local `ahflc` binary to provide real compile/verify/format
 * endpoints for the playground frontend. This is the server-side wrapper
 * slice; the long-term goal is a WASM-compiled compiler for fully
 * client-side operation.
 */
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { parseAhflcOutput, type StructuredResult } from './parser.js';

interface ApiResult {
    success: boolean;
    stdout: string;
    stderr: string;
    exitCode: number;
    elapsedMs: number;
    formatted?: string;
    structured?: StructuredResult;
}

function resolveAhflcPath(): string {
    return (
        process.env.AHFLC_PATH ??
        join(import.meta.dirname, '..', '..', '..', 'build', 'dev', 'src', 'tooling', 'cli', 'ahflc')
    );
}

function runAhflc(args: string[], source: string): Promise<ApiResult> {
    const ahflc = resolveAhflcPath();
    const workDir = mkdtempSync(join(tmpdir(), 'ahfl-playground-'));
    const sourceFile = join(workDir, 'input.ahfl');
    writeFileSync(sourceFile, source, 'utf-8');

    const start = performance.now();
    return new Promise((resolve) => {
        const proc = spawn(ahflc, [...args, sourceFile], {
            cwd: workDir,
            stdio: ['ignore', 'pipe', 'pipe'],
        });

        let stdout = '';
        let stderr = '';
        proc.stdout.on('data', (chunk) => (stdout += chunk));
        proc.stderr.on('data', (chunk) => (stderr += chunk));

        proc.on('close', (code) => {
            const elapsedMs = performance.now() - start;
            const result: ApiResult = {
                success: code === 0,
                stdout,
                stderr,
                exitCode: code ?? -1,
                elapsedMs,
            };

            // For format, read back the formatted file.
            if (args[0] === 'fmt' && code === 0) {
                try {
                    result.formatted = readFileSync(sourceFile, 'utf-8');
                } catch {
                    // Format may not have modified the file; keep original.
                    result.formatted = source;
                }
            }

            rmSync(workDir, { recursive: true, force: true });
            resolve(result);
        });

        proc.on('error', (err) => {
            rmSync(workDir, { recursive: true, force: true });
            resolve({
                success: false,
                stdout: '',
                stderr: `Failed to start ahflc: ${err.message}`,
                exitCode: -1,
                elapsedMs: performance.now() - start,
            });
        });
    });
}

function readBody(req: IncomingMessage): Promise<string> {
    return new Promise((resolve, reject) => {
        let body = '';
        req.on('data', (chunk) => (body += chunk));
        req.on('end', () => resolve(body));
        req.on('error', reject);
    });
}

function sendJson(res: ServerResponse, status: number, data: unknown): void {
    const json = JSON.stringify(data);
    res.writeHead(status, {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(json),
    });
    res.end(json);
}

export function createApiMiddleware() {
    return async (req: IncomingMessage, res: ServerResponse, next: () => void) => {
        if (req.method !== 'POST') {
            next();
            return;
        }

        const url = req.url ?? '';
        const body = await readBody(req);
        let source: string;
        try {
            source = JSON.parse(body).source ?? '';
        } catch {
            sendJson(res, 400, { error: 'invalid JSON body' });
            return;
        }

        if (typeof source !== 'string') {
            sendJson(res, 400, { error: 'source must be a string' });
            return;
        }

        if (url === '/compile') {
            const result = await runAhflc(['check'], source);
            result.structured = parseAhflcOutput(
                result.stdout,
                result.stderr,
                result.success,
                result.elapsedMs,
            );
            sendJson(res, 200, result);
        } else if (url === '/verify') {
            const result = await runAhflc(['check', '--verify=on'], source);
            result.structured = parseAhflcOutput(
                result.stdout,
                result.stderr,
                result.success,
                result.elapsedMs,
            );
            sendJson(res, 200, result);
        } else if (url === '/format') {
            const result = await runAhflc(['fmt'], source);
            sendJson(res, 200, result);
        } else if (url === '/build') {
            const result = await runAhflc(['build', '--verify=off', '--output', 'output.wasm'], source);
            sendJson(res, 200, result);
        } else {
            next();
        }
    };
}
