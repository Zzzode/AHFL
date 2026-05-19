/**
 * Monaco Editor Configuration for AHFL
 *
 * In production, this would configure the Monaco editor with:
 * - AHFL language definition (tokenization)
 * - Auto-completion
 * - Hover info
 * - Error highlighting
 */

interface EditorConfig {
    theme: 'dark' | 'light';
    fontSize: number;
    tabSize: number;
    minimap: boolean;
    lineNumbers: boolean;
}

const defaultConfig: EditorConfig = {
    theme: 'dark',
    fontSize: 14,
    tabSize: 4,
    minimap: false,
    lineNumbers: true
};

// AHFL language token definitions for Monaco
const ahflLanguageDefinition = {
    keywords: [
        'agent', 'workflow', 'capability', 'struct', 'enum',
        'state', 'transition', 'input', 'output', 'context',
        'contract', 'flow', 'node', 'on', 'when', 'then',
        'if', 'else', 'match', 'return', 'let', 'const',
        'import', 'export', 'module', 'package',
        'invariant', 'ensures', 'requires', 'assert'
    ],
    types: [
        'String', 'Int', 'Float', 'Bool', 'List', 'Map',
        'Optional', 'Result', 'Void', 'Any'
    ],
    operators: [
        '->', '=>', '::', ':', '=', '==', '!=',
        '<', '>', '<=', '>=', '&&', '||', '!'
    ]
};

// Example snippets
const ahflSnippets = [
    {
        label: 'agent',
        insertText: 'agent ${1:Name} {\n  states {\n    ${2:idle}\n  }\n  transitions {\n    ${3:idle} -> ${4:running}\n  }\n}',
        documentation: 'Define a new agent'
    },
    {
        label: 'workflow',
        insertText: 'workflow ${1:Name} {\n  node ${2:start} {\n    ${3:// logic}\n  }\n}',
        documentation: 'Define a new workflow'
    },
    {
        label: 'struct',
        insertText: 'struct ${1:Name} {\n  ${2:field}: ${3:String};\n}',
        documentation: 'Define a new struct'
    }
];

function createEditor(container: HTMLElement, config: EditorConfig = defaultConfig): void {
    // In production, this would initialize Monaco
    // For the playground stub, we use a simple textarea
    console.log('[Editor] Initialized with config:', config);
    console.log('[Editor] Language definition loaded:', ahflLanguageDefinition.keywords.length, 'keywords');
    console.log('[Editor] Snippets loaded:', ahflSnippets.length);
}

export { EditorConfig, defaultConfig, ahflLanguageDefinition, ahflSnippets, createEditor };
