const fs = require('fs');
const os = require('os');
const path = require('path');
const { runTests } = require('@vscode/test-electron');

function resolveVSCodeExecutable() {
  if (process.env.VSCODE_TEST_EXECUTABLE) {
    return process.env.VSCODE_TEST_EXECUTABLE;
  }
  if (process.platform === 'darwin') {
    const appExecutable = '/Applications/Visual Studio Code.app/Contents/MacOS/Code';
    if (fs.existsSync(appExecutable)) {
      return appExecutable;
    }
  }
  return undefined;
}

async function main() {
  const extensionDevelopmentPath = path.resolve(__dirname, '..');
  const repoRoot = path.resolve(extensionDevelopmentPath, '..', '..');
  const defaultServerPath = path.join(repoRoot, 'build', 'dev', 'src', 'tooling', 'lsp', 'ahfl-lsp');
  const serverPath = process.env.AHFL_VSCODE_TEST_SERVER || defaultServerPath;
  const workspaceRootPath = path.join(repoRoot, 'tests', 'integration', 'check_ok');
  const extensionTestsPath = path.resolve(__dirname, 'suite', 'index.js');

  if (!fs.existsSync(serverPath)) {
    throw new Error(`AHFL_VSCODE_TEST_SERVER does not exist: ${serverPath}`);
  }
  if (!fs.existsSync(workspaceRootPath)) {
    throw new Error(`AHFL hover test workspace does not exist: ${workspaceRootPath}`);
  }

  const extensionTestsEnv = {
    AHFL_VSCODE_TEST_SERVER: serverPath,
  };
  if (process.env.AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT) {
    extensionTestsEnv.AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT =
      process.env.AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT;
  }

  const profileParent = process.platform === 'darwin' ? '/tmp' : os.tmpdir();
  const profileRoot = fs.mkdtempSync(path.join(profileParent, 'ahfl-vsc-'));
  const workspacePath = path.join(profileRoot, 'ahfl-extension-test.code-workspace');
  fs.writeFileSync(
    workspacePath,
    `${JSON.stringify(
      {
        folders: [{ path: workspaceRootPath, name: 'check_ok' }],
      },
      null,
      2
    )}\n`,
    'utf8'
  );
  try {
    await runTests({
      vscodeExecutablePath: resolveVSCodeExecutable(),
      extensionDevelopmentPath,
      extensionTestsPath,
      launchArgs: [
        workspacePath,
        `--user-data-dir=${path.join(profileRoot, 'user-data')}`,
        `--extensions-dir=${path.join(profileRoot, 'extensions')}`,
        '--disable-workspace-trust',
        '--skip-welcome',
        '--skip-release-notes',
      ],
      extensionTestsEnv,
    });
  } finally {
    if (process.env.AHFL_VSCODE_KEEP_TEST_PROFILE !== '1') {
      fs.rmSync(profileRoot, { recursive: true, force: true });
    } else {
      console.log(`Preserved VS Code test profile at ${profileRoot}`);
    }
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
