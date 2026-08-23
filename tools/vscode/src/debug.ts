import * as path from 'path';

// Pure resolution helpers for the AHFL debug adapter (`ahfl-dap`).
//
// These are kept free of the `vscode` module so they can be unit-tested by
// bundling this file in isolation, mirroring the approach used for
// `toolchain.ts`. The `extension.ts` glue supplies the platform, the bundled
// path, and a filesystem probe.

export interface DebugAdapterResolution {
    // Value of the `ahfl.debugAdapterPath` setting (may be empty / whitespace).
    configuredPath: string;
    // Absolute path to the bundled `server/ahfl-dap[.exe]`, if the extension
    // ships one. Used only when no explicit path is configured.
    bundledPath: string;
    // Probe for the bundled binary. Injected so tests need no real files.
    fileExists(candidate: string): boolean;
}

export function debugAdapterExecutableName(platform: NodeJS.Platform): string {
    return platform === 'win32' ? 'ahfl-dap.exe' : 'ahfl-dap';
}

export function bundledDebugAdapterPath(extensionFsPath: string, platform: NodeJS.Platform): string {
    return path.join(extensionFsPath, 'server', debugAdapterExecutableName(platform));
}

// Resolves the command used to launch the debug adapter. Precedence mirrors the
// language server: explicit configuration wins, then the bundled release
// binary, then the bare executable name resolved through PATH.
export function resolveDebugAdapterCommand(resolution: DebugAdapterResolution): string {
    const trimmed = resolution.configuredPath.trim();
    if (trimmed.length > 0) {
        return trimmed;
    }

    if (resolution.bundledPath.length > 0 && resolution.fileExists(resolution.bundledPath)) {
        return resolution.bundledPath;
    }

    // Fall back to PATH lookup; the executable name is platform independent
    // because the extension is launched by VS Code, not a shell.
    return 'ahfl-dap';
}
