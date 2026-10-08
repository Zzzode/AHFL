/**
 * AHFL Playground Compiler Client
 *
 * Calls the server-side ahflc wrapper API for real compile/verify/format.
 */

export interface ApiResult {
    success: boolean;
    stdout: string;
    stderr: string;
    exitCode: number;
    elapsedMs: number;
    formatted?: string;
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
