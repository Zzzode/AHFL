const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const extensionRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(extensionRoot, '..', '..');
const outputPath =
  process.env.AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT ||
  path.join(extensionRoot, 'dist', 'problems-transcript.json');
const transcriptControlPath = path.join(extensionRoot, '.vscode-test', 'diagnostics-transcript-path.txt');

function existingServerPath() {
  if (process.env.AHFL_VSCODE_TEST_SERVER) {
    return process.env.AHFL_VSCODE_TEST_SERVER;
  }

  const candidates = [
    path.join(repoRoot, 'build', 'dev', 'src', 'tooling', 'lsp', 'ahfl-lsp'),
    path.join(repoRoot, 'build', 'release', 'src', 'tooling', 'lsp', 'ahfl-lsp'),
  ];
  return candidates.find((candidate) => fs.existsSync(candidate));
}

function assertScenario(transcript, scenario, predicate) {
  const snapshot = transcript.snapshots.find((entry) => entry.scenario === scenario);
  assert.ok(snapshot, `missing diagnostics transcript scenario: ${scenario}`);
  assert.ok(predicate(snapshot), `diagnostics transcript scenario failed validation: ${scenario}`);
}

function validateTranscript() {
  const transcript = JSON.parse(fs.readFileSync(outputPath, 'utf8'));
  assert.strictEqual(transcript.schema, 'ahfl.vscode.problemsTranscript.v1');
  assert.strictEqual(transcript.source, 'vscode.languages.getDiagnostics');
  assert.ok(Array.isArray(transcript.snapshots), 'expected transcript snapshots array');

  assertScenario(
    transcript,
    'broken-file.error-published',
    (snapshot) =>
      snapshot.problemCount > 0 &&
      snapshot.diagnostics.some((diagnostic) => diagnostic.source === 'ahfl' && diagnostic.severity === 'error')
  );
  assertScenario(
    transcript,
    'broken-file.fixed-cleared',
    (snapshot) => snapshot.problemCount === 0 && snapshot.diagnostics.length === 0
  );
  assertScenario(transcript, 'watched-files.before-import-change', (snapshot) => snapshot.problemCount === 0);
  assertScenario(
    transcript,
    'watched-files.after-import-change',
    (snapshot) =>
      snapshot.problemCount > 0 &&
      snapshot.diagnostics.some((diagnostic) => diagnostic.source === 'ahfl' && diagnostic.severity === 'error')
  );
}

function main() {
  const serverPath = existingServerPath();
  assert.ok(serverPath, 'expected build/dev or build/release ahfl-lsp before generating transcript');

  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.mkdirSync(path.dirname(transcriptControlPath), { recursive: true });
  fs.rmSync(outputPath, { force: true });
  fs.writeFileSync(transcriptControlPath, outputPath, 'utf8');

  try {
    const result = spawnSync(process.execPath, [path.join(__dirname, 'runTest.js')], {
      cwd: extensionRoot,
      env: {
        ...process.env,
        AHFL_VSCODE_TEST_SERVER: serverPath,
        AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT: outputPath,
      },
      stdio: 'inherit',
    });
    assert.strictEqual(result.status, 0, `extension host diagnostics transcript run failed: ${result.status}`);
    assert.ok(fs.existsSync(outputPath), `expected diagnostics transcript output: ${outputPath}`);
  } finally {
    fs.rmSync(transcriptControlPath, { force: true });
  }

  validateTranscript();
  console.log(`Problems diagnostics transcript written to ${outputPath}`);
}

try {
  main();
} catch (error) {
  console.error(error);
  process.exit(1);
}
