import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('ahfl');
    const serverCommand = config.get<string>('serverPath', 'ahfl-lsp');
    const serverArgs = config.get<string[]>('serverArgs', []);
    
    const serverOptions: ServerOptions = {
        command: serverCommand,
        args: serverArgs,
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'ahfl' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.ahfl'),
        },
    };

    client = new LanguageClient(
        'ahfl-language-server',
        'AHFL Language Server',
        serverOptions,
        clientOptions
    );

    // Register CodeLens provider
    const codeLensProvider = new AhflCodeLensProvider();
    context.subscriptions.push(
        vscode.languages.registerCodeLensProvider({ language: 'ahfl' }, codeLensProvider)
    );

    // Start the client
    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

class AhflCodeLensProvider implements vscode.CodeLensProvider {
    provideCodeLenses(document: vscode.TextDocument): vscode.CodeLens[] {
        const lenses: vscode.CodeLens[] = [];
        const text = document.getText();
        const lines = text.split('\n');

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            
            // Agent declaration
            const agentMatch = line.match(/^\s*agent\s+(\w+)\s*\{/);
            if (agentMatch) {
                const range = new vscode.Range(i, 0, i, line.length);
                lenses.push(new vscode.CodeLens(range, {
                    title: `Agent: ${agentMatch[1]}`,
                    command: '',
                }));
            }

            // Workflow declaration
            const workflowMatch = line.match(/^\s*workflow\s+(\w+)\s*\{/);
            if (workflowMatch) {
                const range = new vscode.Range(i, 0, i, line.length);
                lenses.push(new vscode.CodeLens(range, {
                    title: `Workflow: ${workflowMatch[1]}`,
                    command: '',
                }));
            }
        }

        return lenses;
    }
}
