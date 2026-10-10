/**
 * Monaco Editor Configuration for AHFL
 *
 * Creates the editor instance. The AHFL language itself (tokenizer, theme,
 * completion provider) is registered by registerAhflLanguage() in
 * ahfl-language.ts; createEditor calls it first so the editor is usable
 * standalone, and main.ts also calls it before creating the editor.
 */
import * as monaco from 'monaco-editor';
import { registerAhflLanguage } from './ahfl-language';

let editorInstance: monaco.editor.IStandaloneCodeEditor | null = null;

export function createEditor(container: HTMLElement, initialValue: string = ''): monaco.editor.IStandaloneCodeEditor {
    registerAhflLanguage();

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

/**
 * Reveal a position in the editor and place the cursor there.
 * Line and column are 1-based (matching ahflc diagnostic output).
 */
export function revealPosition(line: number, column: number): void {
    if (!editorInstance) return;
    const position = { lineNumber: line, column: column };
    editorInstance.revealPositionInCenter(position);
    editorInstance.setPosition(position);
    editorInstance.focus();
}
