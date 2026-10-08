/**
 * AHFL Playground Output Renderer
 *
 * Renders structured ahflc results (diagnostics, stats, verification)
 * into user-friendly HTML instead of raw CLI text.
 */
import type { Diagnostic, StructuredResult, VerificationInfo } from './compiler';

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

    let html = `<div class="diag ${cls}">`;
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
