/**
 * AHFL Output Parser
 *
 * Parses the human-readable ahflc stdout/stderr into structured data
 * so the playground can render diagnostics and verification results
 * in a user-friendly format instead of raw CLI text.
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

// Matches: error [CODE]: message (file:line:col)
//          warning [CODE]: message (file:line:col)
//          note [CODE]: message (file:line:col)
const DIAG_HEADER =
    /^(error|warning|note)\s+\[([^\]]+)\]:\s+(.+?)\s+\(([^:]+):(\d+):(\d+)\)\s*$/;

// Matches:   N | source line
const SOURCE_LINE = /^\s+\d+\s+\|\s(.*)$/;

// Matches:     | marker
const SOURCE_MARKER = /^\s+\|\s(\s*[\s~^]+)\s*$/;

// Matches: ok: checked N top-level declaration(s), M symbol(s), K reference(s), L named type(s)
const STATS_LINE =
    /ok:\s+checked\s+(\d+)\s+top-level\s+declaration\(s\),\s+(\d+)\s+symbol\(s\),\s+(\d+)\s+reference\(s\),\s+(\d+)\s+named\s+type\(s\)/;

// Matches: ok: formal verification passed with N proven specification(s)
const VERIFY_PASSED = /ok:\s+formal\s+verification\s+passed\s+with\s+(\d+)\s+proven\s+specification\(s\)/;

// Matches key: value lines in verification output
const VERIFY_FIELD = /^(\w+):\s+(.+)$/;

// The detached_source_unit note appears on every single-file compile;
// it is noise in the playground context.
const DETACHED_NOTE = 'N::detached_source_unit';

export function parseAhflcOutput(
    stdout: string,
    stderr: string,
    success: boolean,
    elapsedMs: number,
): StructuredResult {
    const diagnostics: Diagnostic[] = [];
    let stats: CompileStats | undefined;
    let verification: VerificationInfo | undefined;

    // Parse stderr for diagnostics and verification info.
    const lines = stderr.split('\n');
    let currentDiag: Diagnostic | null = null;
    let inVerification = false;
    const verifyFields: Record<string, string> = {};

    for (const line of lines) {
        // Try diagnostic header
        const headerMatch = line.match(DIAG_HEADER);
        if (headerMatch) {
            // Flush previous diagnostic
            if (currentDiag) {
                diagnostics.push(currentDiag);
            }

            const [, level, code, message, file, lineNum, colNum] = headerMatch;
            // Skip the detached_source_unit note — it's noise in playground context
            if (code === DETACHED_NOTE) {
                currentDiag = null;
                continue;
            }

            currentDiag = {
                level: level as Diagnostic['level'],
                code,
                message,
                file,
                line: parseInt(lineNum, 10),
                column: parseInt(colNum, 10),
            };
            continue;
        }

        // Try source line (  N | source)
        const sourceMatch = line.match(SOURCE_LINE);
        if (sourceMatch && currentDiag) {
            currentDiag.sourceLine = sourceMatch[1];
            continue;
        }

        // Try source marker (    | ~~~)
        const markerMatch = line.match(SOURCE_MARKER);
        if (markerMatch && currentDiag) {
            currentDiag.sourceMarker = markerMatch[1];
            continue;
        }

        // Check for verification section
        if (line.startsWith('ok: formal verification')) {
            inVerification = true;
            const passedMatch = line.match(VERIFY_PASSED);
            if (passedMatch) {
                verifyFields['proven'] = passedMatch[1];
            }
            continue;
        }

        // Parse verification key: value fields
        if (inVerification) {
            const fieldMatch = line.match(VERIFY_FIELD);
            if (fieldMatch) {
                verifyFields[fieldMatch[1]] = fieldMatch[2].trim();
            }
            continue;
        }

        // Check for "note:" continuation lines (e.g., "note: create ahfl.toml...")
        if (line.startsWith('  note:') && currentDiag === null) {
            // This is a continuation of the detached_source_unit note; skip it
            continue;
        }
    }

    // Flush last diagnostic
    if (currentDiag) {
        diagnostics.push(currentDiag);
    }

    // Build verification info if we have enough fields
    if (verifyFields['proven'] !== undefined) {
        verification = {
            proven: parseInt(verifyFields['proven'] ?? '0', 10),
            backend: verifyFields['formal_backend'] ?? 'unknown',
            checkerStatus: verifyFields['checker_status'] ?? 'unknown',
            stateSpaceEstimate: parseInt(verifyFields['state_space_estimate'] ?? '0', 10),
            stateSpaceAgents: parseInt(verifyFields['state_space_agents'] ?? '0', 10),
            stateSpaceMaxStatesPerAgent: parseInt(
                verifyFields['state_space_max_states_per_agent'] ?? '0',
                10,
            ),
            stateSpaceTransitions: parseInt(verifyFields['state_space_transitions'] ?? '0', 10),
            likelyTractable: verifyFields['state_space_likely_tractable'] === 'true',
            stateSpaceMethod: verifyFields['state_space_method'] ?? 'unknown',
            checker: verifyFields['checker'] ?? '',
        };
    }

    // Parse stdout for stats
    const statsMatch = stdout.match(STATS_LINE);
    if (statsMatch) {
        stats = {
            declarations: parseInt(statsMatch[1], 10),
            symbols: parseInt(statsMatch[2], 10),
            references: parseInt(statsMatch[3], 10),
            namedTypes: parseInt(statsMatch[4], 10),
        };
    }

    return {
        success,
        diagnostics,
        stats,
        verification,
        elapsedMs,
    };
}
