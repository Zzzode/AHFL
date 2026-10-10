/**
 * AHFL Playground Entry Point
 *
 * Initializes the Monaco editor and wires up the toolbar buttons
 * to the server-side ahflc API.
 */
import { createEditor, getEditorValue, setEditorValue, revealPosition } from './editor';
import { registerAhflLanguage } from './ahfl-language';
import { callApi, type StructuredResult } from './compiler';
import { renderStructured, renderFallback, renderRunResult } from './renderer';
import {
    runWasmWorkflow,
    generateInputSkeleton,
    discoverCapabilities,
    defaultCapabilityMocks,
    capabilityShortName,
    type CapabilityMocks,
    type ExecutionDescriptor,
} from './wasm-runner';
import helloExample from '../examples/hello.ahfl?raw';
import vendingExample from '../examples/vending-machine.ahfl?raw';
import multiAgentExample from '../examples/multi-agent.ahfl?raw';
import matchEnumExample from '../examples/match-enum.ahfl?raw';
import genericsExample from '../examples/generics.ahfl?raw';
import capabilityAgentExample from '../examples/capability-agent.ahfl?raw';
import contractExample from '../examples/contract.ahfl?raw';
import paymentStateMachineExample from '../examples/payment-state-machine.ahfl?raw';
import effectfulWorkflowExample from '../examples/effectful-workflow.ahfl?raw';
import dataPipelineExample from '../examples/data-pipeline.ahfl?raw';

const EXAMPLES: Record<string, string> = {
    hello: helloExample,
    'vending-machine': vendingExample,
    'multi-agent': multiAgentExample,
    'match-enum': matchEnumExample,
    generics: genericsExample,
    'capability-agent': capabilityAgentExample,
    contract: contractExample,
    'payment-state-machine': paymentStateMachineExample,
    'effectful-workflow': effectfulWorkflowExample,
    'data-pipeline': dataPipelineExample,
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

    // Load source from URL hash if present (#code=base64)
    const hash = window.location.hash;
    let initialSource = DEFAULT_SOURCE;
    if (hash.startsWith('#code=')) {
        try {
            const encoded = hash.slice(6);
            initialSource = decodeURIComponent(atob(encoded));
        } catch {
            // Invalid hash; fall back to default
        }
    }

    registerAhflLanguage();
    createEditor(container, initialSource);

    const outputEl = document.getElementById('output-content')!;
    const statusEl = document.getElementById('status')!;
    const buttons = {
        compile: document.getElementById('btn-compile') as HTMLButtonElement,
        verify: document.getElementById('btn-verify') as HTMLButtonElement,
        build: document.getElementById('btn-build') as HTMLButtonElement,
        run: document.getElementById('btn-run') as HTMLButtonElement,
        format: document.getElementById('btn-format') as HTMLButtonElement,
        share: document.getElementById('btn-share') as HTMLButtonElement,
        reset: document.getElementById('btn-reset') as HTMLButtonElement,
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
        } catch (err) {
            const message = err instanceof Error ? err.message : String(err);
            setOutput('error', renderFallback(false, message));
            statusEl.textContent = 'Error';
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
                const sizeKb = ((result.wasmSize ?? 0) / 1024).toFixed(1);
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

    // --- In-browser workflow execution (Run) ---
    // The last successful build's WASM bytes + descriptor are cached so the
    // Execute button can re-run with edited input without rebuilding. The last
    // capability mocks are cached the same way, so edited mock responses
    // survive a re-build of the same workflow.
    let cachedWasmBytes: Uint8Array | null = null;
    let cachedDescriptor: ExecutionDescriptor | null = null;
    let lastRunInput = '';
    let lastMocks: CapabilityMocks | null = null;

    const base64ToBytes = (base64: string): Uint8Array => {
        const binary = atob(base64);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
        return bytes;
    };

    const executeAndRender = async (inputJson: string, buildMs: number, mocks: CapabilityMocks | null) => {
        if (cachedWasmBytes === null || cachedDescriptor === null) {
            setOutput('error', renderFallback(false, 'No built workflow to run. Click Run first.'));
            return;
        }
        const defaults = cachedDescriptor.imports.length > 0
            ? defaultCapabilityMocks(cachedDescriptor)
            : {};
        const capList = cachedDescriptor.imports.map((imp) => {
            const name = capabilityShortName(imp.name);
            return { name, mock: mocks?.[name] ?? defaults[name] ?? '' };
        });
        const start = performance.now();
        try {
            const runResult = await runWasmWorkflow(
                cachedWasmBytes, cachedDescriptor, inputJson, mocks ?? undefined);
            const elapsed = buildMs + (performance.now() - start);
            setOutput('success', renderRunResult(
                runResult, inputJson, elapsed, capList.length > 0 ? capList : undefined));
            statusEl.textContent = runResult.status === 'completed' ? 'Ran' : 'Suspended';
        } catch (err) {
            const message = err instanceof Error ? err.message : String(err);
            setOutput('error', renderRunResult(
                { status: 'failed', outputJson: null, stateTrace: [], transitions: 0, completedNodes: null, error: message },
                inputJson,
                buildMs + (performance.now() - start),
                capList.length > 0 ? capList : undefined,
            ));
            statusEl.textContent = 'Error';
        }
    };

    buttons.run.addEventListener('click', () =>
        withButton(buttons.run, 'Run', async () => {
            statusEl.textContent = 'Building WASM...';
            const result = await callApi('build', getEditorValue());
            if (!result.success || result.wasmBase64 === undefined) {
                setOutput('error', renderFallback(false, result.stderr || result.stdout || 'Build failed'));
                statusEl.textContent = 'Error';
                return;
            }
            if (result.descriptorJson === undefined) {
                setOutput('error', renderFallback(false,
                    'Build succeeded but no execution descriptor was emitted. The ahflc binary may be out of date.'));
                statusEl.textContent = 'Error';
                return;
            }
            let descriptor: ExecutionDescriptor;
            try {
                descriptor = JSON.parse(result.descriptorJson) as ExecutionDescriptor;
            } catch {
                setOutput('error', renderFallback(false, 'Failed to parse the execution descriptor.'));
                statusEl.textContent = 'Error';
                return;
            }
            cachedWasmBytes = base64ToBytes(result.wasmBase64);
            cachedDescriptor = descriptor;
            // Generate a fresh input skeleton on the first run; keep the
            // user's edited input on subsequent re-builds.
            if (!lastRunInput) {
                try {
                    lastRunInput = generateInputSkeleton(descriptor);
                } catch {
                    lastRunInput = '{}';
                }
            }
            // Capability workflows: seed mocks from the result-type skeletons,
            // keeping the user's edited responses for capabilities still present.
            let requiredCaps: string[];
            try {
                requiredCaps = discoverCapabilities(cachedWasmBytes);
            } catch (err) {
                const message = err instanceof Error ? err.message : String(err);
                setOutput('error', renderFallback(false, message));
                statusEl.textContent = 'Error';
                return;
            }
            if (requiredCaps.length > 0) {
                const defaults = defaultCapabilityMocks(descriptor);
                if (lastMocks === null) {
                    lastMocks = defaults;
                } else {
                    const merged: CapabilityMocks = {};
                    for (const [name, mock] of Object.entries(defaults)) {
                        merged[name] = lastMocks[name] ?? mock;
                    }
                    lastMocks = merged;
                }
            } else {
                lastMocks = null;
            }
            statusEl.textContent = 'Executing...';
            await executeAndRender(lastRunInput, result.elapsedMs, lastMocks);
        }),
    );

    // Execute / Re-run-with-mocks buttons: re-run with the edited input and
    // mock responses (delegated because the buttons are re-created on every
    // render).
    outputEl.addEventListener('click', (e) => {
        const target = (e.target as HTMLElement).closest('#btn-execute, #btn-rerun-mocks') as HTMLElement | null;
        if (target === null) return;
        const inputEl = document.getElementById('run-input') as HTMLTextAreaElement | null;
        lastRunInput = inputEl?.value ?? lastRunInput;
        const mocks: CapabilityMocks = {};
        outputEl.querySelectorAll<HTMLTextAreaElement>('.cap-mock-input').forEach((el) => {
            const cap = el.dataset.cap;
            if (cap !== undefined) mocks[cap] = el.value;
        });
        lastMocks = mocks;
        target.setAttribute('disabled', 'true');
        target.textContent = 'Executing...';
        executeAndRender(lastRunInput, 0, mocks).finally(() => {
            target.removeAttribute('disabled');
            target.textContent = target.id === 'btn-rerun-mocks' ? 'Re-run with mocks' : 'Execute';
        });
    });

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

    buttons.share.addEventListener('click', async () => {
        const source = getEditorValue();
        const encoded = btoa(encodeURIComponent(source));
        const url = `${window.location.origin}${window.location.pathname}#code=${encoded}`;
        try {
            await navigator.clipboard.writeText(url);
            statusEl.textContent = 'Share link copied to clipboard';
        } catch {
            // Clipboard API may fail in some contexts; fall back to prompt
            window.prompt('Copy this URL to share:', url);
            statusEl.textContent = 'Share URL generated';
        }
    });

    buttons.reset.addEventListener('click', () => {
        setEditorValue(DEFAULT_SOURCE);
        setOutput('info', 'Ready. Write some AHFL code and click Compile.');
        statusEl.textContent = 'Reset to default';
        // Clear the URL hash
        history.replaceState(null, '', window.location.pathname);
    });

    // Keyboard shortcuts: Ctrl/Cmd+Enter = Compile, Ctrl/Cmd+Shift+Enter = Verify
    document.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
            e.preventDefault();
            if (e.shiftKey) {
                buttons.verify.click();
            } else {
                buttons.compile.click();
            }
        }
    });

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
