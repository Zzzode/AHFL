import * as path from 'path';

export interface AhflToolchainProfile {
    workspaceFolder: string;
    sysroot: string;
}

export interface AhflToolchainOptions {
    defaultSysroot?: string;
    bundledSysroot: string;
    profiles: AhflToolchainProfile[];
}

export interface WorkspaceConfigurationItem {
    scopeUri?: string | null;
    section?: string | null;
}

export interface WorkspaceConfigurationParams {
    items: WorkspaceConfigurationItem[];
}

export interface UriLike {
    scheme: string;
    fsPath: string;
    toString(): string;
}

export interface WorkspaceFolderLike {
    uri: UriLike;
}

export interface WorkspaceConfigurationLike {
    get<T>(section: string, defaultValue: T): T;
}

export interface ToolchainWorkspaceLike {
    workspaceFolders?: readonly WorkspaceFolderLike[];
    getConfiguration(section: string, scope?: UriLike): WorkspaceConfigurationLike;
    getWorkspaceFolder(resource: UriLike): WorkspaceFolderLike | undefined;
    parseUri(value: string): UriLike | undefined;
}

export function toolchainOptionsFromConfiguration(
    workspace: ToolchainWorkspaceLike,
    extensionFsPath: string
): AhflToolchainOptions {
    const folders = workspace.workspaceFolders ?? [];
    const profiles: AhflToolchainProfile[] = [];

    for (const folder of folders) {
        const folderConfig = workspace.getConfiguration('ahfl', folder.uri);
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

    const config = workspace.getConfiguration('ahfl');
    const defaultFolder = folders.length > 0 ? folders[0] : undefined;
    const configuredDefault = resolveSysrootSetting(
        config.get<string>('toolchain.sysroot', ''),
        defaultFolder
    );

    const options: AhflToolchainOptions = {
        bundledSysroot: extensionFsPath,
        profiles,
    };
    if (configuredDefault.length > 0) {
        options.defaultSysroot = configuredDefault;
    }
    return options;
}

export function workspaceConfigurationFromRequest(
    workspace: ToolchainWorkspaceLike,
    params: WorkspaceConfigurationParams,
    defaultConfiguration: unknown[]
): unknown[] {
    return params.items.map((item, index) => {
        if (item.section === 'ahfl.toolchain') {
            return toolchainConfigurationForScope(workspace, item.scopeUri ?? undefined);
        }
        return defaultConfiguration[index] ?? null;
    });
}

export function resolveSysrootSetting(
    value: string | undefined,
    folder: WorkspaceFolderLike | undefined
): string {
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

function toolchainConfigurationForScope(
    workspace: ToolchainWorkspaceLike,
    scopeUri: string | undefined
): { sysroot: string } {
    const resource = uriFromScope(workspace, scopeUri);
    const folder = resource ? workspace.getWorkspaceFolder(resource) : undefined;
    const config = workspace.getConfiguration('ahfl', resource);
    const sysroot = resolveSysrootSetting(config.get<string>('toolchain.sysroot', ''), folder);

    return {
        sysroot: sysroot.length > 0 ? sysroot : '',
    };
}

function uriFromScope(
    workspace: ToolchainWorkspaceLike,
    scopeUri: string | undefined
): UriLike | undefined {
    if (!scopeUri) {
        return undefined;
    }
    return workspace.parseUri(scopeUri);
}
