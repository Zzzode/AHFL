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
    wasmBase64?: string;
    wasmSize?: number;
    descriptorJson?: string;
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

/**
 * Extract the first `pub workflow <Name>` from the source.
 * Returns null if no workflow is found.
 */
function extractWorkflowName(source: string): string | null {
    const match = source.match(/pub\s+workflow\s+(\w+)/);
    return match ? match[1] : null;
}

/**
 * Rewrite the module declaration to `module playground::main;`
 * so the package graph can resolve it under the playground prefix.
 * If no module declaration exists, prepend one.
 */
function normalizeModuleDecl(source: string): string {
    const moduleRe = /^module\s+[\w:]+;/m;
    if (moduleRe.test(source)) {
        return source.replace(moduleRe, 'module playground::main;');
    }
    return `module playground::main;\n\n${source}`;
}

/**
 * Build a WASM binary from the source. This requires a manifest because
 * `ahflc build` needs a package entry point for workflow programs.
 */
async function runBuild(source: string): Promise<ApiResult> {
    const ahflc = resolveAhflcPath();
    const workDir = mkdtempSync(join(tmpdir(), 'ahfl-playground-build-'));
    const sourceFile = join(workDir, 'main.ahfl');
    const manifestFile = join(workDir, 'ahfl.toml');
    const wasmFile = join(workDir, 'output.wasm');
    const descriptorFile = join(workDir, 'descriptor.json');

    const workflowName = extractWorkflowName(source);
    if (!workflowName) {
        rmSync(workDir, { recursive: true, force: true });
        return {
            success: false,
            stdout: '',
            stderr: 'No pub workflow found in source. Build requires a workflow entry point.',
            exitCode: 1,
            elapsedMs: 0,
        };
    }

    const normalized = normalizeModuleDecl(source);
    writeFileSync(sourceFile, normalized, 'utf-8');

    const manifest = `manifest_version = 1

[package]
name = "playground"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "playground"
root = "."

[exports]
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "playground::main::${workflowName}"
exports = [{ kind = "workflow", name = "playground::main::${workflowName}" }]

[dependencies]
std = { source = "sysroot" }
`;
    writeFileSync(manifestFile, manifest, 'utf-8');

    const start = performance.now();
    return new Promise((resolve) => {
        const proc = spawn(
            ahflc,
            [
                'build',
                '--manifest',
                manifestFile,
                '--verify=off',
                '--output',
                wasmFile,
                '--emit-descriptor',
                descriptorFile,
            ],
            { cwd: workDir, stdio: ['ignore', 'pipe', 'pipe'] },
        );

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

            if (code === 0) {
                try {
                    const wasmBytes = readFileSync(wasmFile);
                    result.wasmBase64 = wasmBytes.toString('base64');
                    result.wasmSize = wasmBytes.length;
                } catch {
                    result.success = false;
                    result.stderr = 'Build succeeded but WASM output file was not found.';
                }
                // The descriptor is a build sidecar; a missing descriptor is a
                // soft failure (the binary is still usable for download) but
                // the in-browser Run feature needs it.
                try {
                    result.descriptorJson = readFileSync(descriptorFile, 'utf-8');
                } catch {
                    result.stderr +=
                        (result.stderr ? '\n' : '') +
                        'Warning: build succeeded but the execution descriptor was not emitted.';
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
            const result = await runBuild(source);
            sendJson(res, 200, result);
        } else {
            next();
        }
    };
}
