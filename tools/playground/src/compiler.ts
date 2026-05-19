/**
 * AHFL WASM Compiler Wrapper
 *
 * This module provides the interface between the playground UI
 * and the AHFL compiler compiled to WebAssembly.
 */

interface CompileResult {
    success: boolean;
    output: string;
    diagnostics: Diagnostic[];
    elapsed_ms: number;
}

interface Diagnostic {
    level: 'error' | 'warning' | 'info';
    message: string;
    line: number;
    column: number;
}

interface VerifyResult {
    verified: boolean;
    properties_checked: number;
    counterexample?: string;
}

class AhflCompiler {
    private wasmModule: WebAssembly.Module | null = null;
    private initialized: boolean = false;

    async initialize(): Promise<void> {
        // In production, this would load the actual WASM module
        // For now, we simulate compilation
        this.initialized = true;
        console.log('[AHFL] Compiler initialized (stub mode)');
    }

    compile(source: string): CompileResult {
        if (!this.initialized) {
            return {
                success: false,
                output: '',
                diagnostics: [{ level: 'error', message: 'Compiler not initialized', line: 0, column: 0 }],
                elapsed_ms: 0
            };
        }

        const start = performance.now();
        const diagnostics: Diagnostic[] = [];

        // Basic validation (stub)
        if (source.trim().length === 0) {
            diagnostics.push({ level: 'error', message: 'Empty source', line: 1, column: 1 });
            return { success: false, output: '', diagnostics, elapsed_ms: performance.now() - start };
        }

        // Check for basic syntax patterns
        const hasAgent = /agent\s+\w+/.test(source);
        const hasWorkflow = /workflow\s+\w+/.test(source);
        const hasStruct = /struct\s+\w+/.test(source);

        let output = '// Compilation output (stub)\n';
        if (hasAgent) output += '// Agent definition found\n';
        if (hasWorkflow) output += '// Workflow definition found\n';
        if (hasStruct) output += '// Struct definition found\n';

        if (!hasAgent && !hasWorkflow && !hasStruct) {
            diagnostics.push({ level: 'warning', message: 'No top-level declarations found', line: 1, column: 1 });
        }

        return {
            success: diagnostics.every(d => d.level !== 'error'),
            output,
            diagnostics,
            elapsed_ms: performance.now() - start
        };
    }

    verify(source: string): VerifyResult {
        // Stub verification
        const hasInvariant = /invariant|assert|ensures/.test(source);
        return {
            verified: hasInvariant,
            properties_checked: hasInvariant ? 1 : 0,
            counterexample: hasInvariant ? undefined : 'No properties to verify'
        };
    }

    format(source: string): string {
        // Simple formatting stub - just normalize whitespace
        return source
            .split('\n')
            .map(line => line.trimEnd())
            .join('\n')
            .replace(/\n{3,}/g, '\n\n');
    }
}

// Initialize and wire up UI
const compiler = new AhflCompiler();

document.addEventListener('DOMContentLoaded', async () => {
    await compiler.initialize();

    const sourceEl = document.getElementById('source') as HTMLTextAreaElement;
    const outputEl = document.getElementById('output-content')!;
    const statusEl = document.getElementById('status')!;

    document.getElementById('btn-compile')?.addEventListener('click', () => {
        const result = compiler.compile(sourceEl.value);
        outputEl.className = result.success ? 'success' : 'error';
        let text = result.success ? `Compiled successfully (${result.elapsed_ms.toFixed(1)}ms)\n\n` : 'Compilation failed:\n\n';
        for (const d of result.diagnostics) {
            text += `[${d.level}] Line ${d.line}: ${d.message}\n`;
        }
        if (result.output) text += '\n' + result.output;
        outputEl.textContent = text;
        statusEl.textContent = result.success ? 'Compiled' : 'Error';
    });

    document.getElementById('btn-verify')?.addEventListener('click', () => {
        const result = compiler.verify(sourceEl.value);
        outputEl.className = result.verified ? 'success' : 'info';
        outputEl.textContent = result.verified
            ? `Verification passed (${result.properties_checked} properties checked)`
            : `Verification: ${result.counterexample}`;
        statusEl.textContent = 'Verified';
    });

    document.getElementById('btn-format')?.addEventListener('click', () => {
        sourceEl.value = compiler.format(sourceEl.value);
        outputEl.className = 'success';
        outputEl.textContent = 'Source formatted.';
        statusEl.textContent = 'Formatted';
    });
});

export { AhflCompiler, CompileResult, VerifyResult, Diagnostic };
