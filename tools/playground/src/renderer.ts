/**
 * AHFL Playground Output Renderer
 *
 * Renders structured ahflc results (diagnostics, stats, verification)
 * into user-friendly HTML instead of raw CLI text.
 */
import type { Diagnostic, StructuredResult, VerificationInfo } from './compiler';
import type { RunResult } from './wasm-runner';

const LEVEL_ICONS: Record<Diagnostic['level'], string> = {
    error: '✕',
    warning: '⚠',
    note: 'ℹ',
};

const LEVEL_CLASSES: Record<Diagnostic['level'], string> = {
    error: 'diag-error',
    warning: 'diag-warning',
    note: 'diag-note',
};

function escapeHtml(text: string): string {
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function renderDiagnostic(diag: Diagnostic): string {
    const icon = LEVEL_ICONS[diag.level];
    const cls = LEVEL_CLASSES[diag.level];
    const loc = `${diag.line}:${diag.column}`;

    let html = `<div class="diag ${cls} diag-clickable" data-line="${diag.line}" data-column="${diag.column}">`;
    html += `<div class="diag-header">`;
    html += `<span class="diag-icon">${icon}</span>`;
    html += `<span class="diag-level">${diag.level}</span>`;
    html += `<span class="diag-code">[${escapeHtml(diag.code)}]</span>`;
    html += `<span class="diag-loc">${escapeHtml(loc)}</span>`;
    html += `</div>`;
    html += `<div class="diag-message">${escapeHtml(diag.message)}</div>`;

    if (diag.sourceLine !== undefined) {
        html += `<div class="diag-source">`;
        html += `<span class="diag-source-line">${escapeHtml(diag.sourceLine)}</span>`;
        if (diag.sourceMarker !== undefined) {
            html += `<span class="diag-source-marker">${escapeHtml(diag.sourceMarker)}</span>`;
        }
        html += `</div>`;
    }

    html += `</div>`;
    return html;
}

function renderStats(stats: NonNullable<StructuredResult['stats']>): string {
    return `<div class="result-stats">
        <span class="stat-item">${stats.declarations} declarations</span>
        <span class="stat-sep">·</span>
        <span class="stat-item">${stats.symbols} symbols</span>
        <span class="stat-sep">·</span>
        <span class="stat-item">${stats.references} references</span>
        <span class="stat-sep">·</span>
        <span class="stat-item">${stats.namedTypes} named types</span>
    </div>`;
}

function renderVerification(v: VerificationInfo): string {
    let html = `<div class="verify-section">`;
    html += `<div class="verify-header">`;
    html += `<span class="verify-icon">✓</span>`;
    html += `<span class="verify-title">Formal Verification Passed</span>`;
    html += `</div>`;
    html += `<div class="verify-body">`;
    html += `<div class="verify-row"><span class="verify-label">Properties proven</span><span class="verify-value">${v.proven}</span></div>`;
    html += `<div class="verify-row"><span class="verify-label">Backend</span><span class="verify-value">${escapeHtml(v.backend)}</span></div>`;
    html += `<div class="verify-row"><span class="verify-label">State space</span><span class="verify-value">~${v.stateSpaceEstimate.toLocaleString()} states</span></div>`;
    html += `<div class="verify-row"><span class="verify-label">Agents</span><span class="verify-value">${v.stateSpaceAgents}</span></div>`;
    html += `<div class="verify-row"><span class="verify-label">Transitions</span><span class="verify-value">${v.stateSpaceTransitions}</span></div>`;
    html += `<div class="verify-row"><span class="verify-label">Tractable</span><span class="verify-value ${v.likelyTractable ? 'verify-ok' : 'verify-warn'}">${v.likelyTractable ? 'yes' : 'no'}</span></div>`;
    html += `</div>`;
    html += `</div>`;
    return html;
}

/**
 * Render a structured compile/verify result into HTML.
 */
export function renderStructured(result: StructuredResult): string {
    let html = '';

    // Summary line
    if (result.success) {
        html += `<div class="result-summary result-ok">`;
        html += `<span class="result-icon">✓</span>`;
        html += `<span class="result-text">Compiled successfully</span>`;
        html += `<span class="result-time">${result.elapsedMs.toFixed(0)}ms</span>`;
        html += `</div>`;
    } else {
        const errorCount = result.diagnostics.filter((d) => d.level === 'error').length;
        html += `<div class="result-summary result-fail">`;
        html += `<span class="result-icon">✕</span>`;
        html += `<span class="result-text">Compilation failed</span>`;
        if (errorCount > 0) {
            html += `<span class="result-count">${errorCount} error${errorCount > 1 ? 's' : ''}</span>`;
        }
        html += `<span class="result-time">${result.elapsedMs.toFixed(0)}ms</span>`;
        html += `</div>`;
    }

    // Stats (only on success)
    if (result.success && result.stats) {
        html += renderStats(result.stats);
    }

    // Diagnostics
    if (result.diagnostics.length > 0) {
        html += `<div class="diag-list">`;
        for (const diag of result.diagnostics) {
            html += renderDiagnostic(diag);
        }
        html += `</div>`;
    }

    // Verification results
    if (result.verification) {
        html += renderVerification(result.verification);
    }

    return html;
}

/**
 * Render a fallback message when structured parsing is unavailable
 * (e.g. API error, binary not found).
 */
export function renderFallback(success: boolean, text: string): string {
    const cls = success ? 'result-ok' : 'result-fail';
    return `<div class="result-summary ${cls}">
        <span class="result-icon">${success ? '✓' : '✕'}</span>
        <span class="result-text">${escapeHtml(text)}</span>
    </div>`;
}

/**
 * One capability's editable mock response: the capability's short source name
 * and the wire JSON the playground returns on every invocation of it.
 */
export interface CapabilityMockInfo {
    name: string;
    mock: string;
}

/**
 * Render the in-browser workflow run result: a status summary, the editable
 * input JSON, the capability mock editor (when the workflow imports
 * capabilities), the output JSON, observed capability calls, the state trace,
 * and run stats. The Execute button (id="btn-execute") re-runs with the edited
 * input without rebuilding; the Re-run with mocks button (id="btn-rerun-mocks")
 * re-runs with the edited mock responses.
 */
export function renderRunResult(
    result: RunResult,
    inputJson: string,
    elapsedMs: number,
    capabilities?: CapabilityMockInfo[],
): string {
    let html = '<div class="run-view">';

    // Status summary
    const completed = result.status === 'completed';
    const suspended = result.status === 'suspended';
    const summaryCls = completed ? 'result-ok' : suspended ? 'result-fail' : 'result-fail';
    const summaryIcon = completed ? '✓' : suspended ? '⏸' : '✕';
    const summaryText = completed
        ? 'Workflow completed'
        : suspended
          ? 'Workflow suspended (pending capability)'
          : 'Workflow failed';
    html += `<div class="result-summary ${summaryCls}">
        <span class="result-icon">${summaryIcon}</span>
        <span class="result-text">${summaryText}</span>
        <span class="result-time">${elapsedMs.toFixed(0)}ms</span>
    </div>`;

    // Input editor
    html += `<div class="run-input-section">
        <div class="run-section-header">Workflow Input (JSON)</div>
        <textarea id="run-input" class="run-input-editor" spellcheck="false">${escapeHtml(inputJson)}</textarea>
        <button id="btn-execute" class="btn-execute">Execute</button>
    </div>`;

    // Capability mock editor
    if (capabilities !== undefined && capabilities.length > 0) {
        html += `<div class="cap-section">
            <div class="run-section-header">Capability Mocks (wire JSON returned per call)</div>`;
        for (const cap of capabilities) {
            html += `<div class="cap-mock">
                <div class="cap-mock-name">${escapeHtml(cap.name)}</div>
                <textarea class="cap-mock-input" data-cap="${escapeHtml(cap.name)}" spellcheck="false">${escapeHtml(cap.mock)}</textarea>
            </div>`;
        }
        html += `<button id="btn-rerun-mocks" class="btn-execute">Re-run with mocks</button>
        </div>`;
    }

    // Output
    if (result.outputJson !== null) {
        let pretty: string;
        try {
            pretty = JSON.stringify(JSON.parse(result.outputJson), null, 2);
        } catch {
            pretty = result.outputJson;
        }
        html += `<div class="run-output-section">
            <div class="run-section-header">Output</div>
            <pre class="run-output-json">${escapeHtml(pretty)}</pre>
        </div>`;
    }

    // Stats
    const stats: string[] = [];
    if (result.completedNodes !== null) {
        stats.push(`${result.completedNodes} node${result.completedNodes === 1 ? '' : 's'} completed`);
    }
    stats.push(`${result.transitions} transition${result.transitions === 1 ? '' : 's'}`);
    html += `<div class="run-stats-bar">${stats.map((s) => `<span>${escapeHtml(s)}</span>`).join('<span class="stat-sep">·</span>')}</div>`;

    // Capability calls observed during the run
    if (result.capabilityCalls !== undefined && result.capabilityCalls.length > 0) {
        html += `<div class="cap-calls-section">
            <div class="run-section-header">Capability Calls</div>`;
        for (const call of result.capabilityCalls) {
            let prettyArgs: string;
            try {
                prettyArgs = JSON.stringify(JSON.parse(call.argument), null, 2);
            } catch {
                prettyArgs = call.argument;
            }
            html += `<div class="cap-call">
                <span class="cap-call-name">${escapeHtml(call.name)}</span>
                <pre class="cap-call-args">${escapeHtml(prettyArgs)}</pre>
            </div>`;
        }
        html += `</div>`;
    }

    // State trace
    if (result.stateTrace.length > 0) {
        const traceParts: string[] = [];
        for (let i = 0; i < result.stateTrace.length; i++) {
            const entry = result.stateTrace[i];
            const shortAgent = entry.agent.split('::').pop() ?? entry.agent;
            traceParts.push(`<span class="run-trace-node">${escapeHtml(shortAgent)}: ${escapeHtml(entry.state)}</span>`);
            if (i < result.stateTrace.length - 1) {
                traceParts.push('<span class="run-trace-arrow">→</span>');
            }
        }
        html += `<div class="run-trace-section">
            <div class="run-section-header">State Trace</div>
            <div class="run-trace">${traceParts.join('')}</div>
        </div>`;
    }

    if (result.error) {
        html += `<div class="run-suspended">${escapeHtml(result.error)}</div>`;
    }

    html += '</div>';
    return html;
}
