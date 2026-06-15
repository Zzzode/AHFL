const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const { runTests } = require('@vscode/test-electron');

function resolveVSCodeExecutable() {
  if (process.env.VSCODE_TEST_EXECUTABLE) {
    return process.env.VSCODE_TEST_EXECUTABLE;
  }
  const result = spawnSync('which', ['code'], { encoding: 'utf8' });
  if (result.status === 0) {
    const executable = result.stdout.trim();
    if (executable.length > 0) {
      return executable;
    }
  }
  return undefined;
}

async function main() {
  const extensionDevelopmentPath = path.resolve(__dirname, '..');
  const repoRoot = path.resolve(extensionDevelopmentPath, '..', '..');
  const defaultServerPath = path.join(repoRoot, 'build', 'dev', 'src', 'tooling', 'lsp', 'ahfl-lsp');
  const serverPath = process.env.AHFL_VSCODE_TEST_SERVER || defaultServerPath;
  const workspacePath = path.join(repoRoot, 'tests', 'integration', 'check_ok');
  const extensionTestsPath = path.resolve(__dirname, 'suite');

  if (!fs.existsSync(serverPath)) {
    throw new Error(`AHFL_VSCODE_TEST_SERVER does not exist: ${serverPath}`);
  }
  if (!fs.existsSync(workspacePath)) {
    throw new Error(`AHFL hover test workspace does not exist: ${workspacePath}`);
  }

  await runTests({
    vscodeExecutablePath: resolveVSCodeExecutable(),
    extensionDevelopmentPath,
    extensionTestsPath,
    launchArgs: [
      workspacePath,
      '--disable-extensions',
      '--disable-workspace-trust',
      '--skip-welcome',
      '--skip-release-notes',
    ],
    extensionTestsEnv: {
      AHFL_VSCODE_TEST_SERVER: serverPath,
    },
  });
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
