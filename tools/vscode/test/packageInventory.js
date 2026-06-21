const assert = require('assert');
const path = require('path');
const { spawnSync } = require('child_process');

const extensionRoot = path.resolve(__dirname, '..');

function runVsceLs() {
  const vsceBin = path.join(
    extensionRoot,
    'node_modules',
    '.bin',
    process.platform === 'win32' ? 'vsce.cmd' : 'vsce'
  );
  const result = spawnSync(
    vsceBin,
    ['ls', '--no-dependencies'],
    {
      cwd: extensionRoot,
      encoding: 'utf8',
    }
  );

  if (result.status !== 0) {
    assert.fail(
      `vsce package inventory failed with exit code ${result.status}\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`
    );
  }

  return result.stdout
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
}

function main() {
  const files = runVsceLs();
  const fileSet = new Set(files);
  const serverBinary = process.platform === 'win32' ? 'server/ahfl-lsp.exe' : 'server/ahfl-lsp';
  const requiredFiles = [
    'package.json',
    'README.md',
    'LICENSE',
    'CHANGELOG.md',
    'language-configuration.json',
    'out/extension.js',
    serverBinary,
    'syntaxes/ahfl.tmLanguage.json',
    'snippets/ahfl.json',
  ];

  for (const file of requiredFiles) {
    assert.ok(fileSet.has(file), `expected Marketplace package inventory to include ${file}`);
  }

  const forbiddenPrefixes = ['.vscode/', '.vscode-test/', 'dist/', 'node_modules/', 'src/', 'test/'];
  const forbiddenFiles = ['package-lock.json', 'pnpm-lock.yaml', 'tsconfig.json'];
  for (const file of files) {
    assert.ok(
      !forbiddenPrefixes.some((prefix) => file.startsWith(prefix)),
      `Marketplace package inventory includes development path ${file}`
    );
    assert.ok(!forbiddenFiles.includes(file), `Marketplace package inventory includes development file ${file}`);
  }

  console.log(`Verified Marketplace package inventory (${files.length} files)`);
}

try {
  main();
} catch (error) {
  console.error(error);
  process.exit(1);
}
