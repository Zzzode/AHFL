/**
 * AHFL Language Registration for Monaco
 *
 * Registers the AHFL language (Monarch tokenizer, dark theme, and a keyword
 * completion provider) with the Monaco editor. Kept in its own module so the
 * language definition is independent of the editor instance; call
 * registerAhflLanguage() once before creating an editor. Registration is
 * idempotent.
 */
import * as monaco from 'monaco-editor';

const AHFL_KEYWORDS = [
    // Modules / items
    'module', 'pub', 'use', 'import', 'struct', 'enum', 'fn', 'type', 'trait',
    'impl', 'capability', 'predicate', 'agent', 'workflow', 'flow', 'contract',
    'node', 'after',
    // Agent members
    'states', 'state', 'initial', 'final', 'transition', 'transitions',
    'capabilities', 'input', 'output', 'context', 'effects', 'need',
    // Statements / expressions
    'return', 'goto', 'if', 'else', 'match', 'let', 'const', 'for', 'in',
    'while', 'loop', 'break', 'continue',
    // Contracts / verification
    'requires', 'ensures', 'invariant', 'decreases', 'assume', 'assert',
    // Literals / common identifiers
    'true', 'false', 'some', 'none', 'ok', 'err', 'unit', 'self', 'Self',
];

const AHFL_TYPE_KEYWORDS = [
    'String', 'Int', 'Float', 'Bool', 'List', 'Set', 'Map', 'Optional',
    'Result', 'Unit', 'Any', 'Decimal', 'Duration', 'Timestamp', 'UUID',
];

const ahflLanguageDefinition: monaco.languages.IMonarchLanguage = {
    defaultToken: '',
    keywords: AHFL_KEYWORDS,
    typeKeywords: AHFL_TYPE_KEYWORDS,
    operators: [
        '->', '=>', '::', '=', '==', '!=', '<', '>', '<=', '>=', '&&', '||',
        '!', '+', '-', '*', '/', '%',
    ],
    symbols: /[=><!~?:&|+\-*\/\^%]+/,
    tokenizer: {
        root: [
            // Attributes: #[...]
            [/#\[/, { token: 'keyword', next: '@attr' }],

            // Comments
            [/\/\/.*$/, 'comment'],
            [/\/\*/, 'comment', '@comment'],

            // Strings
            [/"([^"\\]|\\.)*$/, 'string.invalid'],
            [/"/, { token: 'string.quote', bracket: '@open', next: '@string' }],

            // Numbers: hex, float, duration suffix, integer
            [/0[xX][0-9a-fA-F]+/, 'number.hex'],
            [/\d+\.\d+([eE][\-+]?\d+)?/, 'number.float'],
            [/\d+(ms|us|s|m|h)\b/, 'number'],
            [/\d+/, 'number'],

            // Identifiers and keywords
            [/[a-zA-Z_]\w*/, {
                cases: {
                    '@keywords': 'keyword',
                    '@typeKeywords': 'type',
                    '^[A-Z]': 'type',
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
        attr: [
            [/[^\]]+/, 'keyword'],
            [/\]/, 'keyword', '@pop'],
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

const ahflTheme: monaco.editor.IStandaloneThemeData = {
    base: 'vs-dark',
    inherit: true,
    rules: [
        { token: 'keyword', foreground: '569cd6' },
        { token: 'type', foreground: '4ec9b0' },
        { token: 'comment', foreground: '6a9955' },
        { token: 'string', foreground: 'ce9178' },
        { token: 'string.escape', foreground: 'd7ba7d' },
        { token: 'number', foreground: 'b5cea8' },
        { token: 'number.hex', foreground: 'b5cea8' },
        { token: 'number.float', foreground: 'b5cea8' },
        { token: 'operator', foreground: 'd4d4d4' },
    ],
    colors: {
        'editor.background': '#1e1e1e',
    },
};

let registered = false;

/**
 * Register the AHFL language, theme, and keyword completion provider with
 * Monaco. Idempotent: calling it more than once is a no-op.
 */
export function registerAhflLanguage(): void {
    if (registered) return;
    registered = true;

    monaco.languages.register({ id: 'ahfl' });
    monaco.languages.setMonarchTokensProvider('ahfl', ahflLanguageDefinition);
    monaco.languages.setLanguageConfiguration('ahfl', {
        comments: {
            lineComment: '//',
            blockComment: ['/*', '*/'],
        },
        brackets: [
            ['{', '}'],
            ['[', ']'],
            ['(', ')'],
        ],
        autoClosingPairs: [
            { open: '{', close: '}' },
            { open: '[', close: ']' },
            { open: '(', close: ')' },
            { open: '"', close: '"' },
        ],
        surroundingPairs: [
            { open: '{', close: '}' },
            { open: '[', close: ']' },
            { open: '(', close: ')' },
            { open: '"', close: '"' },
        ],
    });
    monaco.editor.defineTheme('ahfl-dark', ahflTheme);

    monaco.languages.registerCompletionItemProvider('ahfl', {
        provideCompletionItems: (model, position) => {
            const word = model.getWordUntilPosition(position);
            const range = new monaco.Range(
                position.lineNumber, word.startColumn,
                position.lineNumber, word.endColumn,
            );
            const suggestions: monaco.languages.CompletionItem[] = AHFL_KEYWORDS.map(
                (keyword) => ({
                    label: keyword,
                    kind: monaco.languages.CompletionItemKind.Keyword,
                    insertText: keyword,
                    range,
                }),
            );
            return { suggestions };
        },
    });
}
