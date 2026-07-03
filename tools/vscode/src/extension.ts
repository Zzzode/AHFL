import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions, State } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let statusBarItem: vscode.StatusBarItem | undefined;
let configChangeListener: vscode.Disposable | undefined;
let diagnosticChangeListener: vscode.Disposable | undefined;
let activeEditorChangeListener: vscode.Disposable | undefined;
let clientStateChangeListener: vscode.Disposable | undefined;

interface AhflHoverOptions {
    detailLevel: string;
    showSource: boolean;
    maxFacts: number;
    markupKind: string;
}

interface AhflToolchainProfile {
    workspaceFolder: string;
    sysroot: string;
}

interface AhflToolchainOptions {
    defaultSysroot: string;
    profiles: AhflToolchainProfile[];
}

interface WorkspaceConfigurationItem {
    scopeUri?: string | null;
    section?: string | null;
}

interface WorkspaceConfigurationParams {
    items: WorkspaceConfigurationItem[];
}

export function activate(context: vscode.ExtensionContext) {
    // Create status bar item
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 0);
    statusBarItem.name = 'AHFL';
    statusBarItem.command = 'workbench.actions.view.problems';
    statusBarItem.tooltip = 'AHFL Language Server (click to view problems)';
    context.subscriptions.push(statusBarItem);

    // Create and start the language client
    client = createClient(context);
    client.start();

    // Track client state for status bar
    clientStateChangeListener = client.onDidChangeState((event) => {
        updateStatusBar();
    });

    // Update status bar when diagnostics change
    diagnosticChangeListener = vscode.languages.onDidChangeDiagnostics(() => {
        updateStatusBar();
    });
    context.subscriptions.push(diagnosticChangeListener);

    // Update status bar when active editor changes
    activeEditorChangeListener = vscode.window.onDidChangeActiveTextEditor(() => {
        updateStatusBar();
    });
    context.subscriptions.push(activeEditorChangeListener);

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('ahfl.restartServer', async () => {
            await restartLanguageClient(context);
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('ahfl.formatDocument', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) {
                return;
            }
            if (editor.document.languageId !== 'ahfl') {
                vscode.window.showInformationMessage('AHFL formatter is only available for AHFL files.');
                return;
            }
            await vscode.commands.executeCommand('editor.action.formatDocument');
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('ahfl.showOutput', () => {
            if (client) {
                client.outputChannel.show();
            }
        })
    );

    // Listen for configuration changes
    configChangeListener = vscode.workspace.onDidChangeConfiguration(async (e) => {
        if (!e.affectsConfiguration('ahfl')) {
            return;
        }

        // If server path or args changed, restart the client
        if (e.affectsConfiguration('ahfl.serverPath') || e.affectsConfiguration('ahfl.serverArgs')) {
            await restartLanguageClient(context);
            return;
        }

        // For other config changes, send didChangeConfiguration notification
        if (client && client.state === State.Running) {
            const config = vscode.workspace.getConfiguration('ahfl');
            client.sendNotification('workspace/didChangeConfiguration', {
                settings: {
                    ahfl: {
                        hover: hoverOptionsFromConfiguration(config),
                        toolchain: toolchainOptionsFromConfiguration(context),
                    },
                },
            });
        }
    });
    context.subscriptions.push(configChangeListener);

    // Initial status bar update
    updateStatusBar();
}

export function deactivate(): Thenable<void> | undefined {
    if (statusBarItem) {
        statusBarItem.dispose();
        statusBarItem = undefined;
    }

    if (clientStateChangeListener) {
        clientStateChangeListener.dispose();
        clientStateChangeListener = undefined;
    }

    if (configChangeListener) {
        configChangeListener.dispose();
        configChangeListener = undefined;
    }

    if (diagnosticChangeListener) {
        diagnosticChangeListener.dispose();
        diagnosticChangeListener = undefined;
    }

    if (activeEditorChangeListener) {
        activeEditorChangeListener.dispose();
        activeEditorChangeListener = undefined;
    }

    if (!client) {
        return undefined;
    }

    const result = client.stop();
    client = undefined;
    return result;
}

function createClient(context: vscode.ExtensionContext): LanguageClient {
    const config = vscode.workspace.getConfiguration('ahfl');
    const serverCommand = resolveServerCommand(context, config.get<string>('serverPath', ''));
    const serverArgs = config.get<string[]>('serverArgs', []);

    const serverOptions: ServerOptions = {
        command: serverCommand,
        args: serverArgs,
        options: {
            env: {
                ...process.env,
            },
        },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'ahfl' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.ahfl'),
        },
        initializationOptions: {
            ahfl: {
                hover: hoverOptionsFromConfiguration(config),
                toolchain: toolchainOptionsFromConfiguration(context),
            },
        },
        middleware: {
            workspace: {
                configuration: async (params, token, next) => {
                    const nextResult = await Promise.resolve(next(params, token));
                    const defaultConfiguration = Array.isArray(nextResult) ? nextResult : [];
                    return workspaceConfigurationFromRequest(params, defaultConfiguration);
                },
            },
        },
    };

    return new LanguageClient(
        'ahfl-language-server',
        'AHFL Language Server',
        serverOptions,
        clientOptions
    );
}

function hoverOptionsFromConfiguration(config: vscode.WorkspaceConfiguration): AhflHoverOptions {
    return {
        detailLevel: config.get<string>('hover.detailLevel', 'standard'),
        showSource: config.get<boolean>('hover.showSource', false),
        maxFacts: config.get<number>('hover.maxFacts', 3),
        markupKind: config.get<string>('hover.markupKind', 'auto'),
    };
}

function toolchainOptionsFromConfiguration(context: vscode.ExtensionContext): AhflToolchainOptions {
    const folders = vscode.workspace.workspaceFolders ?? [];
    const profiles: AhflToolchainProfile[] = [];

    for (const folder of folders) {
        const folderConfig = vscode.workspace.getConfiguration('ahfl', folder.uri);
        const sysroot = resolveSysrootSetting(
            folderConfig.get<string>('toolchain.sysroot', ''),
            folder
        );
        if (sysroot.length > 0) {
            profiles.push({
                workspaceFolder: folder.uri.toString(),
                sysroot,
            });
        }
    }

    const config = vscode.workspace.getConfiguration('ahfl');
    const defaultFolder = folders.length > 0 ? folders[0] : undefined;
    const configuredDefault = resolveSysrootSetting(
        config.get<string>('toolchain.sysroot', ''),
        defaultFolder
    );

    return {
        defaultSysroot:
            configuredDefault.length > 0 ? configuredDefault : context.extensionUri.fsPath,
        profiles,
    };
}

function workspaceConfigurationFromRequest(
    params: WorkspaceConfigurationParams,
    defaultConfiguration: unknown[]
): unknown[] {
    return params.items.map((item, index) => {
        if (item.section === 'ahfl.toolchain') {
            return toolchainConfigurationForScope(item.scopeUri ?? undefined);
        }
        return defaultConfiguration[index] ?? null;
    });
}

function toolchainConfigurationForScope(scopeUri: string | undefined): { sysroot: string } {
    const resource = uriFromScope(scopeUri);
    const folder = resource ? vscode.workspace.getWorkspaceFolder(resource) : undefined;
    const config = vscode.workspace.getConfiguration('ahfl', resource);
    const sysroot = resolveSysrootSetting(config.get<string>('toolchain.sysroot', ''), folder);

    return {
        sysroot: sysroot.length > 0 ? sysroot : '',
    };
}

function uriFromScope(scopeUri: string | undefined): vscode.Uri | undefined {
    if (!scopeUri) {
        return undefined;
    }
    try {
        return vscode.Uri.parse(scopeUri);
    } catch (_err) {
        return undefined;
    }
}

function resolveSysrootSetting(value: string | undefined, folder: vscode.WorkspaceFolder | undefined): string {
    const trimmed = (value ?? '').trim();
    if (trimmed.length === 0) {
        return '';
    }

    const workspaceRoot = folder?.uri.scheme === 'file' ? folder.uri.fsPath : '';
    const expanded = workspaceRoot.length > 0
        ? trimmed.replace(/\$\{workspaceFolder\}/g, workspaceRoot)
        : trimmed;
    if (path.isAbsolute(expanded) || workspaceRoot.length === 0) {
        return expanded;
    }
    return path.resolve(workspaceRoot, expanded);
}

async function restartLanguageClient(context: vscode.ExtensionContext): Promise<void> {
    if (client) {
        try {
            await client.stop();
        } catch (err) {
            // Ignore stop errors, continue with restart
        }
        client = undefined;
    }

    client = createClient(context);

    clientStateChangeListener = client.onDidChangeState(() => {
        updateStatusBar();
    });

    client.start();
    updateStatusBar();

    vscode.window.showInformationMessage('AHFL language server restarted.');
}

function resolveServerCommand(context: vscode.ExtensionContext, configuredPath: string): string {
    const trimmedPath = configuredPath.trim();
    if (trimmedPath.length > 0) {
        return trimmedPath;
    }

    const executableName = process.platform === 'win32' ? 'ahfl-lsp.exe' : 'ahfl-lsp';
    const bundledServer = context.asAbsolutePath(path.join('server', executableName));
    if (fs.existsSync(bundledServer)) {
        return bundledServer;
    }

    return 'ahfl-lsp';
}

function updateStatusBar(): void {
    if (!statusBarItem) {
        return;
    }

    const editor = vscode.window.activeTextEditor;

    // If no active editor or not an AHFL file, hide the status bar item
    if (!editor || editor.document.languageId !== 'ahfl') {
        statusBarItem.hide();
        return;
    }

    // Show the status bar item
    statusBarItem.show();

    // Determine status based on client state
    if (!client) {
        statusBarItem.text = '$(warning) AHFL';
        statusBarItem.tooltip = 'AHFL Language Server: not running';
        return;
    }

    if (client.state === State.Starting) {
        statusBarItem.text = '$(sync~spin) AHFL';
        statusBarItem.tooltip = 'AHFL Language Server: starting...';
        return;
    }

    if (client.state !== State.Running) {
        statusBarItem.text = '$(error) AHFL';
        statusBarItem.tooltip = 'AHFL Language Server: error';
        return;
    }

    // Count errors and warnings for the active document
    const diagnostics = vscode.languages.getDiagnostics(editor.document.uri);
    let errorCount = 0;
    let warningCount = 0;

    for (const diagnostic of diagnostics) {
        if (diagnostic.severity === vscode.DiagnosticSeverity.Error) {
            errorCount++;
        } else if (diagnostic.severity === vscode.DiagnosticSeverity.Warning) {
            warningCount++;
        }
    }

    // Build status text
    let text = '$(check) AHFL';
    if (errorCount > 0 || warningCount > 0) {
        const parts: string[] = [];
        if (errorCount > 0) {
            parts.push(`$(error) ${errorCount}`);
        }
        if (warningCount > 0) {
            parts.push(`$(warning) ${warningCount}`);
        }
        text = `AHFL ${parts.join('  ')}`;
    }

    statusBarItem.text = text;
    statusBarItem.tooltip = `AHFL Language Server: running\nErrors: ${errorCount}\nWarnings: ${warningCount}\nClick to view problems`;
}
