/**
 * AHFL Playground Compiler Client
 *
 * Calls the server-side ahflc wrapper API for real compile/verify/format.
 */

export interface Diagnostic {
    level: 'error' | 'warning' | 'note';
    code: string;
    message: string;
    file: string;
    line: number;
    column: number;
    sourceLine?: string;
    sourceMarker?: string;
}

export interface CompileStats {
    declarations: number;
    symbols: number;
    references: number;
    namedTypes: number;
}

export interface VerificationInfo {
    proven: number;
    backend: string;
    checkerStatus: string;
    stateSpaceEstimate: number;
    stateSpaceAgents: number;
    stateSpaceMaxStatesPerAgent: number;
    stateSpaceTransitions: number;
    likelyTractable: boolean;
    stateSpaceMethod: string;
    checker: string;
}

export interface StructuredResult {
    success: boolean;
    diagnostics: Diagnostic[];
    stats?: CompileStats;
    verification?: VerificationInfo;
    elapsedMs: number;
}

export interface ApiResult {
    success: boolean;
    stdout: string;
    stderr: string;
    exitCode: number;
    elapsedMs: number;
    formatted?: string;
    structured?: StructuredResult;
    wasmBase64?: string;
    wasmSize?: number;
}

export async function callApi(endpoint: string, source: string): Promise<ApiResult> {
    const resp = await fetch(`/api/${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source }),
    });
    if (!resp.ok) {
        return {
            success: false,
            stdout: '',
            stderr: `API error: ${resp.status} ${resp.statusText}`,
            exitCode: resp.status,
            elapsedMs: 0,
        };
    }
    return resp.json();
}
