/**
 * Monaco Editor Configuration for AHFL
 *
 * Registers the AHFL language with Monaco and creates the editor instance.
 */
import * as monaco from 'monaco-editor';

// AHFL language token definitions for Monaco
const ahflLanguageDefinition: monaco.languages.IMonarchLanguage = {
    defaultToken: '',
    keywords: [
        'agent', 'workflow', 'capability', 'struct', 'enum',
        'state', 'transition', 'transitions', 'input', 'output', 'context',
        'contract', 'flow', 'node', 'on', 'when', 'then',
        'if', 'else', 'match', 'return', 'let', 'const',
        'import', 'export', 'module', 'package',
        'invariant', 'ensures', 'requires', 'assert',
        'fn', 'effect', 'pure', 'type', 'alias',
        'true', 'false', 'none', 'some',
    ],
    typeKeywords: [
        'String', 'Int', 'Float', 'Bool', 'List', 'Map',
        'Optional', 'Result', 'Void', 'Any', 'Decimal', 'Duration',
    ],
    operators: [
        '->', '=>', '::', ':', '=', '==', '!=',
        '<', '>', '<=', '>=', '&&', '||', '!',
        '+', '-', '*', '/', '%',
    ],
    symbols: /[=><!~?:&|+\-*\/\^%]+/,
    tokenizer: {
        root: [
            // Comments
            [/\/\/.*$/, 'comment'],
            [/\/\*/, 'comment', '@comment'],

            // Strings
            [/"([^"\\]|\\.)*$/, 'string.invalid'],
            [/"/, { token: 'string.quote', bracket: '@open', next: '@string' }],

            // Numbers
            [/\d*\.\d+([eE][\-+]?\d+)?/, 'number.float'],
            [/\d+/, 'number'],

            // Identifiers and keywords
            [/[a-zA-Z_]\w*/, {
                cases: {
                    '@typeKeywords': 'type',
                    '@keywords': 'keyword',
                    '@default': 'identifier',
                },
            }],

            // Whitespace
            { include: '@whitespace' },

            // Delimiters
            [/[{}()\[\]]/, '@brackets'],
            [/[<>](?!@symbols)/, '@brackets'],
            [/@symbols/, {
                cases: {
                    '@operators': 'operator',
                    '@default': '',
                },
            }],
        ],
        comment: [
            [/[^\/*]+/, 'comment'],
            [/\*\//, 'comment', '@pop'],
            [/[\/*]/, 'comment'],
        ],
        string: [
            [/[^\\"]+/, 'string'],
            [/\\./, 'string.escape'],
            [/"/, { token: 'string.quote', bracket: '@close', next: '@pop' }],
        ],
        whitespace: [
            [/[ \t\r\n]+/, 'white'],
        ],
    },
};

// AHFL dark theme (VS Code-style)
const ahflTheme: monaco.editor.IStandaloneThemeData = {
    base: 'vs-dark',
    inherit: true,
    rules: [
        { token: 'keyword', foreground: '569cd6' },
        { token: 'type', foreground: '4ec9b0' },
        { token: 'comment', foreground: '6a9955' },
        { token: 'string', foreground: 'ce9178' },
        { token: 'number', foreground: 'b5cea8' },
        { token: 'operator', foreground: 'd4d4d4' },
    ],
    colors: {
        'editor.background': '#1e1e1e',
    },
};

let editorInstance: monaco.editor.IStandaloneCodeEditor | null = null;

export function createEditor(container: HTMLElement, initialValue: string = ''): monaco.editor.IStandaloneCodeEditor {
    // Register language and theme once
    monaco.languages.register({ id: 'ahfl' });
    monaco.languages.setMonarchTokensProvider('ahfl', ahflLanguageDefinition);
    monaco.editor.defineTheme('ahfl-dark', ahflTheme);

    editorInstance = monaco.editor.create(container, {
        value: initialValue,
        language: 'ahfl',
        theme: 'ahfl-dark',
        fontSize: 14,
        fontFamily: "'Fira Code', 'Cascadia Code', monospace",
        minimap: { enabled: false },
        lineNumbers: 'on',
        scrollBeyondLastLine: false,
        automaticLayout: true,
        tabSize: 4,
        insertSpaces: true,
    });

    return editorInstance;
}

export function getEditor(): monaco.editor.IStandaloneCodeEditor | null {
    return editorInstance;
}

export function getEditorValue(): string {
    return editorInstance?.getValue() ?? '';
}

export function setEditorValue(value: string): void {
    editorInstance?.setValue(value);
}
