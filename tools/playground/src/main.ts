/**
 * AHFL Playground Entry Point
 *
 * Initializes the Monaco editor and wires up the toolbar buttons
 * to the server-side ahflc API.
 */
import { createEditor, getEditorValue, setEditorValue } from './editor';
import { callApi } from './compiler';
import helloExample from '../examples/hello.ahfl?raw';

const EXAMPLES: Record<string, string> = {
    hello: helloExample,
};

const DEFAULT_SOURCE = `// Welcome to the AHFL Playground
// Write an agent, workflow, or struct definition, then click Compile.

module playground;

agent Greeter {
    states {
        idle,
        greeting,
        done
    }

    transitions {
        idle -> greeting on receive_name,
        greeting -> done on greet_complete
    }

    capabilities {
        text_generation
    }
}

struct GreetRequest {
    name: String;
}

struct GreetResponse {
    message: String;
}

workflow HelloWorkflow {
    input: GreetRequest;
    output: GreetResponse;

    node prepare {
        input: GreetRequest;
        output: String;
    }

    node generate {
        input: String;
        output: GreetResponse;
        capability: text_generation;
    }

    flow {
        prepare -> generate
    }
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
        format: document.getElementById('btn-format') as HTMLButtonElement,
    };

    const setOutput = (className: string, text: string) => {
        outputEl.className = className;
        outputEl.textContent = text;
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
            const text = result.success
                ? `Compiled successfully (${result.elapsedMs.toFixed(0)}ms)\n\n${result.stdout}`
                : `Compilation failed (exit ${result.exitCode}):\n\n${result.stderr || result.stdout}`;
            setOutput(result.success ? 'success' : 'error', text);
            statusEl.textContent = result.success ? 'Compiled' : 'Error';
        }),
    );

    buttons.verify.addEventListener('click', () =>
        withButton(buttons.verify, 'Verify', async () => {
            statusEl.textContent = 'Verifying...';
            const result = await callApi('verify', getEditorValue());
            const text = result.success
                ? `Verification passed (${result.elapsedMs.toFixed(0)}ms)\n\n${result.stdout}`
                : `Verification failed (exit ${result.exitCode}):\n\n${result.stderr || result.stdout}`;
            setOutput(result.success ? 'success' : 'error', text);
            statusEl.textContent = result.success ? 'Verified' : 'Error';
        }),
    );

    buttons.format.addEventListener('click', () =>
        withButton(buttons.format, 'Format', async () => {
            statusEl.textContent = 'Formatting...';
            const result = await callApi('format', getEditorValue());
            if (result.success && result.formatted !== undefined) {
                setEditorValue(result.formatted);
                setOutput('success', `Formatted (${result.elapsedMs.toFixed(0)}ms)`);
                statusEl.textContent = 'Formatted';
            } else {
                setOutput('error', `Format failed:\n\n${result.stderr || result.stdout}`);
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
