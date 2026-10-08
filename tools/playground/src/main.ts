/**
 * AHFL Playground Entry Point
 *
 * Initializes the Monaco editor and wires up the toolbar buttons
 * to the server-side ahflc API.
 */
import { createEditor, getEditorValue, setEditorValue, revealPosition } from './editor';
import { callApi, type StructuredResult } from './compiler';
import { renderStructured, renderFallback } from './renderer';
import helloExample from '../examples/hello.ahfl?raw';

const EXAMPLES: Record<string, string> = {
    hello: helloExample,
};

const DEFAULT_SOURCE = `// Welcome to the AHFL Playground
// Write an agent, workflow, or struct definition, then click Compile.

module playground;

pub struct GreetRequest {
    name: String;
}

pub struct GreetResponse {
    message: String;
}

pub agent Greeter {
    input: GreetRequest;
    context: Unit;
    output: GreetResponse;
    states: [Idle, Greeting, Done];
    initial: Idle;
    final: [Done];
    capabilities: [];
    transition Idle -> Greeting;
    transition Greeting -> Done;
}

flow for Greeter {
    state Idle {
        goto Greeting;
    }

    state Greeting {
        goto Done;
    }

    state Done {
        return GreetResponse { message: "Hello, " + input.name };
    }
}

pub workflow HelloWorkflow {
    input: GreetRequest;
    output: GreetResponse;

    node greet: Greeter(input);

    return: greet;
}
`;

document.addEventListener('DOMContentLoaded', async () => {
    const container = document.getElementById('editor-container')!;
    createEditor(container, DEFAULT_SOURCE);

    const outputEl = document.getElementById('output-content')!;
    const statusEl = document.getElementById('status')!;
    const buttons = {
        compile: document.getElementById('btn-compile') as HTMLButtonElement,
        verify: document.getElementById('btn-verify') as HTMLButtonElement,
        build: document.getElementById('btn-build') as HTMLButtonElement,
        format: document.getElementById('btn-format') as HTMLButtonElement,
    };

    const setOutput = (className: string, html: string) => {
        outputEl.className = className;
        outputEl.innerHTML = html;
    };

    // Click-to-jump: clicking a diagnostic reveals its position in the editor.
    outputEl.addEventListener('click', (e) => {
        const target = (e.target as HTMLElement).closest('.diag-clickable') as HTMLElement | null;
        if (!target) return;
        const line = parseInt(target.dataset.line ?? '0', 10);
        const column = parseInt(target.dataset.column ?? '0', 10);
        if (line > 0 && column > 0) {
            revealPosition(line, column);
        }
    });

    const renderResult = (success: boolean, structured?: StructuredResult, fallbackText?: string) => {
        if (structured) {
            setOutput(success ? 'success' : 'error', renderStructured(structured));
        } else {
            setOutput(success ? 'success' : 'error', renderFallback(success, fallbackText ?? ''));
        }
    };

    const withButton = async (btn: HTMLButtonElement, label: string, fn: () => Promise<void>) => {
        btn.disabled = true;
        btn.textContent = `${label}...`;
        try {
            await fn();
        } finally {
            btn.disabled = false;
            btn.textContent = label;
        }
    };

    buttons.compile.addEventListener('click', () =>
        withButton(buttons.compile, 'Compile', async () => {
            statusEl.textContent = 'Compiling...';
            const result = await callApi('compile', getEditorValue());
            renderResult(
                result.success,
                result.structured,
                result.success ? result.stdout : result.stderr || result.stdout,
            );
            statusEl.textContent = result.success ? 'Compiled' : 'Error';
        }),
    );

    buttons.verify.addEventListener('click', () =>
        withButton(buttons.verify, 'Verify', async () => {
            statusEl.textContent = 'Verifying...';
            const result = await callApi('verify', getEditorValue());
            renderResult(
                result.success,
                result.structured,
                result.success ? result.stdout : result.stderr || result.stdout,
            );
            statusEl.textContent = result.success ? 'Verified' : 'Error';
        }),
    );

    buttons.build.addEventListener('click', () =>
        withButton(buttons.build, 'Build', async () => {
            statusEl.textContent = 'Building WASM...';
            const result = await callApi('build', getEditorValue());
            if (result.success && result.wasmBase64 !== undefined) {
                const sizeKb = (result.wasmSize ?? 0 / 1024).toFixed(1);
                const html = `<div class="result-summary result-ok">
                    <span class="result-icon">✓</span>
                    <span class="result-text">Built WASM binary</span>
                    <span class="result-count">${sizeKb} KB</span>
                    <span class="result-time">${result.elapsedMs.toFixed(0)}ms</span>
                </div>
                <div class="build-download">
                    <a href="data:application/wasm;base64,${result.wasmBase64}" download="output.wasm" class="download-link">Download output.wasm</a>
                </div>`;
                setOutput('success', html);
                statusEl.textContent = 'Built';
            } else {
                setOutput('error', renderFallback(false, result.stderr || result.stdout || 'Build failed'));
                statusEl.textContent = 'Error';
            }
        }),
    );

    buttons.format.addEventListener('click', () =>
        withButton(buttons.format, 'Format', async () => {
            statusEl.textContent = 'Formatting...';
            const result = await callApi('format', getEditorValue());
            if (result.success && result.formatted !== undefined) {
                setEditorValue(result.formatted);
                setOutput('success', renderFallback(true, `Formatted in ${result.elapsedMs.toFixed(0)}ms`));
                statusEl.textContent = 'Formatted';
            } else {
                setOutput('error', renderFallback(false, result.stderr || result.stdout || 'Format failed'));
                statusEl.textContent = 'Error';
            }
        }),
    );

    // Load examples
    const examplesSelect = document.getElementById('examples') as HTMLSelectElement;
    examplesSelect?.addEventListener('change', () => {
        const name = examplesSelect.value;
        if (!name) return;
        const example = EXAMPLES[name];
        if (example !== undefined) {
            setEditorValue(example);
            statusEl.textContent = `Loaded ${name}.ahfl`;
        }
        examplesSelect.value = '';
    });
});
