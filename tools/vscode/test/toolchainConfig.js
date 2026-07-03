const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const esbuild = require('esbuild');

const extensionRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(extensionRoot, '..', '..');

function bundleToolchainModule() {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'ahfl-vscode-toolchain-'));
  const outfile = path.join(tempRoot, 'toolchain.cjs');
  esbuild.buildSync({
    entryPoints: [path.join(extensionRoot, 'src', 'toolchain.ts')],
    bundle: true,
    platform: 'node',
    format: 'cjs',
    target: 'node18',
    outfile,
  });
  return { tempRoot, module: require(outfile) };
}

function fileUri(fsPath) {
  return {
    scheme: 'file',
    fsPath,
    toString() {
      return `file://${fsPath}`;
    },
  };
}

function configuration(values) {
  return {
    get(section, defaultValue) {
      return Object.prototype.hasOwnProperty.call(values, section) ? values[section] : defaultValue;
    },
  };
}

function workspace(globalConfig, folders, scopedConfigByPath) {
  return {
    workspaceFolders: folders,
    getConfiguration(section, scope) {
      assert.strictEqual(section, 'ahfl');
      if (scope && scopedConfigByPath.has(scope.fsPath)) {
        return configuration(scopedConfigByPath.get(scope.fsPath));
      }
      return configuration(globalConfig);
    },
    getWorkspaceFolder(resource) {
      return folders.find((folder) => resource.fsPath.startsWith(folder.uri.fsPath));
    },
    parseUri(value) {
      if (!value.startsWith('file://')) {
        return undefined;
      }
      return fileUri(value.slice('file://'.length));
    },
  };
}

function main() {
  const { tempRoot, module: toolchain } = bundleToolchainModule();
  try {
    const workspaceA = { uri: fileUri(path.join(repoRoot, 'workspace-a')) };
    const workspaceB = { uri: fileUri(path.join(repoRoot, 'workspace-b')) };
    const scopedConfig = new Map([
      [workspaceA.uri.fsPath, { 'toolchain.sysroot': '${workspaceFolder}' }],
      [workspaceB.uri.fsPath, { 'toolchain.sysroot': 'relative-sdk' }],
    ]);
    const host = workspace({ 'toolchain.sysroot': '' }, [workspaceA, workspaceB], scopedConfig);

    const options = toolchain.toolchainOptionsFromConfiguration(host, '/extension-root');
    assert.deepStrictEqual(options, {
      bundledSysroot: '/extension-root',
      profiles: [
        {
          workspaceFolder: workspaceA.uri.toString(),
          sysroot: workspaceA.uri.fsPath,
        },
        {
          workspaceFolder: workspaceB.uri.toString(),
          sysroot: path.join(workspaceB.uri.fsPath, 'relative-sdk'),
        },
      ],
    });

    const defaultHost = workspace(
      { 'toolchain.sysroot': '${workspaceFolder}/default-sdk' },
      [workspaceA],
      new Map()
    );
    const defaultOptions = toolchain.toolchainOptionsFromConfiguration(defaultHost, '/bundled');
    assert.strictEqual(defaultOptions.defaultSysroot, path.join(workspaceA.uri.fsPath, 'default-sdk'));
    assert.strictEqual(defaultOptions.bundledSysroot, '/bundled');
    assert.deepStrictEqual(defaultOptions.profiles, [
      {
        workspaceFolder: workspaceA.uri.toString(),
        sysroot: path.join(workspaceA.uri.fsPath, 'default-sdk'),
      },
    ]);

    const configurationResult = toolchain.workspaceConfigurationFromRequest(
      host,
      {
        items: [
          { section: 'ahfl.toolchain', scopeUri: workspaceB.uri.toString() },
          { section: 'ahfl.hover', scopeUri: workspaceB.uri.toString() },
        ],
      },
      [null, { detailLevel: 'debug' }]
    );
    assert.deepStrictEqual(configurationResult, [
      { sysroot: path.join(workspaceB.uri.fsPath, 'relative-sdk') },
      { detailLevel: 'debug' },
    ]);

    const extensionSource = fs.readFileSync(path.join(extensionRoot, 'src', 'extension.ts'), 'utf8');
    const toolchainSource = fs.readFileSync(path.join(extensionRoot, 'src', 'toolchain.ts'), 'utf8');
    assert.ok(!extensionSource.includes('AHFL_SYSROOT'), 'extension must not inject AHFL_SYSROOT');
    assert.ok(!toolchainSource.includes('AHFL_SYSROOT'), 'toolchain config must not read AHFL_SYSROOT');

    console.log('Verified VS Code toolchain configuration payloads');
  } finally {
    fs.rmSync(tempRoot, { recursive: true, force: true });
  }
}

try {
  main();
} catch (error) {
  console.error(error);
  process.exit(1);
}
