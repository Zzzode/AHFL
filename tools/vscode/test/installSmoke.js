const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const { downloadAndUnzipVSCode, resolveCliArgsFromVSCodeExecutablePath } = require('@vscode/test-electron');

const extensionRoot = path.resolve(__dirname, '..');
const packageJson = require(path.join(extensionRoot, 'package.json'));

function findSystemCodeCli() {
  if (process.env.VSCODE_TEST_CLI) {
    return process.env.VSCODE_TEST_CLI;
  }

  const command = process.platform === 'win32' ? 'where' : 'which';
  const result = spawnSync(command, ['code'], { encoding: 'utf8' });
  if (result.status !== 0) {
    return undefined;
  }
  const executable = result.stdout.split(/\r?\n/).find((line) => line.trim().length > 0);
  return executable ? executable.trim() : undefined;
}

async function resolveCodeCliArgs() {
  const systemCli = findSystemCodeCli();
  if (systemCli) {
    return [systemCli];
  }

  const executable = await downloadAndUnzipVSCode('stable');
  return resolveCliArgsFromVSCodeExecutablePath(executable, { reuseMachineInstall: true });
}

function candidateVsixFiles() {
  const distDir = path.join(extensionRoot, 'dist');
  if (!fs.existsSync(distDir)) {
    return [];
  }
  return fs
    .readdirSync(distDir)
    .filter((entry) => /^ahfl-language-\d+\.\d+\.\d+-[^/]+\.vsix$/.test(entry))
    .map((entry) => path.join(distDir, entry))
    .sort((left, right) => fs.statSync(right).mtimeMs - fs.statSync(left).mtimeMs);
}

function resolveVsixPath() {
  const requested = process.argv[2] || process.env.AHFL_VSCODE_TEST_VSIX;
  if (requested) {
    return path.resolve(process.cwd(), requested);
  }

  const [latest] = candidateVsixFiles();
  assert.ok(
    latest,
    'expected a platform VSIX in tools/vscode/dist; run scripts/package-vscode-vsix-release.sh first'
  );
  return latest;
}

function runCodeCli(baseArgs, args, description) {
  const [command, ...prefixArgs] = baseArgs;
  const result = spawnSync(command, [...prefixArgs, ...args], {
    encoding: 'utf8',
    shell: process.platform === 'win32',
  });

  if (result.status !== 0) {
    assert.fail(
      `${description} failed with exit code ${result.status}\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`
    );
  }
  return result;
}

function findInstalledExtensionDir(extensionsDir) {
  const expectedPrefix = `${packageJson.publisher}.${packageJson.name}-${packageJson.version}`.toLowerCase();
  const entries = fs.existsSync(extensionsDir) ? fs.readdirSync(extensionsDir) : [];
  const match = entries.find((entry) => entry.toLowerCase().startsWith(expectedPrefix));
  assert.ok(match, `expected installed extension directory with prefix ${expectedPrefix}`);
  return path.join(extensionsDir, match);
}

async function main() {
  const vsixPath = resolveVsixPath();
  assert.ok(fs.existsSync(vsixPath), `VSIX does not exist: ${vsixPath}`);
  assert.notStrictEqual(
    path.basename(vsixPath),
    'ahfl-language.vsix',
    'install smoke requires a platform VSIX with a bundled server, not the client-only VSIX'
  );

  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'ahfl-vscode-install-'));
  const userDataDir = path.join(tempRoot, 'user-data');
  const extensionsDir = path.join(tempRoot, 'extensions');
  const profileArgs = [`--user-data-dir=${userDataDir}`, `--extensions-dir=${extensionsDir}`];
  const codeCliArgs = await resolveCodeCliArgs();
  const extensionId = `${packageJson.publisher}.${packageJson.name}`;

  try {
    runCodeCli(
      codeCliArgs,
      [...profileArgs, '--install-extension', vsixPath, '--force'],
      'VSIX installation'
    );

    const listResult = runCodeCli(
      codeCliArgs,
      [...profileArgs, '--list-extensions', '--show-versions'],
      'installed extension listing'
    );
    const installedLine = listResult.stdout
      .split(/\r?\n/)
      .find((line) => line.trim() === `${extensionId}@${packageJson.version}`);
    assert.ok(
      installedLine,
      `expected ${extensionId}@${packageJson.version} in installed extension list\n${listResult.stdout}`
    );

    const installedDir = findInstalledExtensionDir(extensionsDir);
    const serverBinary = path.join(installedDir, 'server', process.platform === 'win32' ? 'ahfl-lsp.exe' : 'ahfl-lsp');
    assert.ok(fs.existsSync(serverBinary), `expected bundled LSP server in installed VSIX: ${serverBinary}`);
    if (process.platform !== 'win32') {
      fs.accessSync(serverBinary, fs.constants.X_OK);
    }

    console.log(`Installed ${installedLine} from ${vsixPath}`);
    console.log(`Bundled server verified at ${serverBinary}`);
  } finally {
    if (process.env.AHFL_VSCODE_KEEP_INSTALL_SMOKE !== '1') {
      fs.rmSync(tempRoot, { recursive: true, force: true });
    } else {
      console.log(`Preserved install smoke profile at ${tempRoot}`);
    }
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
