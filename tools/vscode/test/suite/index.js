const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const HOVER_TIMEOUT_MS = 15_000;
const HOVER_POLL_MS = 250;
const DIAGNOSTIC_TIMEOUT_MS = 15_000;
const DIAGNOSTIC_POLL_MS = 250;
const COMPLETION_TIMEOUT_MS = 15_000;
const COMPLETION_POLL_MS = 250;
const diagnosticsTranscriptControlPath = path.resolve(
  __dirname,
  '..',
  '..',
  '.vscode-test',
  'diagnostics-transcript-path.txt'
);
const defaultDiagnosticsTranscriptPath = path.resolve(__dirname, '..', '..', 'dist', 'problems-transcript.json');
const diagnosticsTranscriptPath = resolveDiagnosticsTranscriptPath();
const diagnosticsTranscript = [];

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function resolveDiagnosticsTranscriptPath() {
  if (process.env.AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT) {
    return process.env.AHFL_VSCODE_DIAGNOSTICS_TRANSCRIPT;
  }
  if (!fs.existsSync(diagnosticsTranscriptControlPath)) {
    return defaultDiagnosticsTranscriptPath;
  }
  return fs.readFileSync(diagnosticsTranscriptControlPath, 'utf8').trim() || defaultDiagnosticsTranscriptPath;
}

function hoverText(hover) {
  return hover.contents
    .map((content) => {
      if (typeof content === 'string') {
        return content;
      }
      return content.value || '';
    })
    .join('\n');
}

function positionForNeedle(document, needle, occurrence = 0) {
  const text = document.getText();
  let searchFrom = 0;
  let offset = -1;
  for (let index = 0; index <= occurrence; index += 1) {
    offset = text.indexOf(needle, searchFrom);
    assert.notStrictEqual(offset, -1, `missing fixture text: ${needle}`);
    searchFrom = offset + needle.length;
  }
  return document.positionAt(offset);
}

function positionAfterNeedle(document, needle, occurrence = 0) {
  const text = document.getText();
  let searchFrom = 0;
  let offset = -1;
  for (let index = 0; index <= occurrence; index += 1) {
    offset = text.indexOf(needle, searchFrom);
    assert.notStrictEqual(offset, -1, `missing fixture text: ${needle}`);
    searchFrom = offset + needle.length;
  }
  return document.positionAt(offset + needle.length);
}

function workspaceRootPath() {
  const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
  assert.ok(workspaceFolder, 'expected hover test workspace folder');
  return workspaceFolder.uri.fsPath;
}

function writeTextFile(filePath, content) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, content, 'utf8');
}

function writePackageManifest(packageRoot, packageName, modulePrefix, exportedModules, targetEntry) {
  const quotedModules = exportedModules.map((moduleName) => `"${moduleName}"`).join(', ');
  writeTextFile(
    path.join(packageRoot, 'ahfl.toml'),
    [
      'manifest_version = 1',
      '',
      '[package]',
      `name = "${packageName}"`,
      'version = "0.1.0"',
      'edition = "2026"',
      'kind = "library"',
      '',
      '[module]',
      `prefix = "${modulePrefix}"`,
      'root = "."',
      '',
      '[exports]',
      `modules = [${quotedModules}]`,
      '',
      '[targets.lib]',
      'kind = "library"',
      `entry = "${targetEntry}"`,
      '',
    ].join('\n')
  );
}

function diagnosticCodeText(diagnostic) {
  if (typeof diagnostic.code === 'string') {
    return diagnostic.code;
  }
  if (diagnostic.code && typeof diagnostic.code.value === 'string') {
    return diagnostic.code.value;
  }
  if (diagnostic.code !== undefined && diagnostic.code !== null) {
    return String(diagnostic.code);
  }
  return '';
}

function diagnosticSeverityName(severity) {
  switch (severity) {
    case vscode.DiagnosticSeverity.Error:
      return 'error';
    case vscode.DiagnosticSeverity.Warning:
      return 'warning';
    case vscode.DiagnosticSeverity.Information:
      return 'information';
    case vscode.DiagnosticSeverity.Hint:
      return 'hint';
    default:
      return `unknown:${severity}`;
  }
}

function transcriptUriLabel(uri) {
  const folders = vscode.workspace.workspaceFolders || [];
  for (const folder of folders) {
    if (uri.scheme === folder.uri.scheme && uri.fsPath.startsWith(`${folder.uri.fsPath}${path.sep}`)) {
      return `${folder.name}/${path.relative(folder.uri.fsPath, uri.fsPath).split(path.sep).join('/')}`;
    }
  }
  return path.basename(uri.fsPath);
}

function diagnosticTranscriptEntry(diagnostic) {
  return {
    source: diagnostic.source || '',
    severity: diagnosticSeverityName(diagnostic.severity),
    code: diagnosticCodeText(diagnostic),
    message: diagnostic.message,
    range: {
      start: {
        line: diagnostic.range.start.line,
        character: diagnostic.range.start.character,
      },
      end: {
        line: diagnostic.range.end.line,
        character: diagnostic.range.end.character,
      },
    },
  };
}

function recordDiagnosticsSnapshot(scenario, uri, diagnostics) {
  if (!diagnosticsTranscriptPath) {
    return;
  }
  diagnosticsTranscript.push({
    scenario,
    source: 'vscode.languages.getDiagnostics',
    document: transcriptUriLabel(uri),
    problemCount: diagnostics.length,
    diagnostics: diagnostics.map(diagnosticTranscriptEntry),
  });
  writeDiagnosticsTranscript();
}

function writeDiagnosticsTranscript() {
  if (!diagnosticsTranscriptPath) {
    return;
  }
  const transcript = {
    schema: 'ahfl.vscode.problemsTranscript.v1',
    source: 'vscode.languages.getDiagnostics',
    snapshots: diagnosticsTranscript,
  };
  fs.mkdirSync(path.dirname(diagnosticsTranscriptPath), { recursive: true });
  fs.writeFileSync(diagnosticsTranscriptPath, `${JSON.stringify(transcript, null, 2)}\n`, 'utf8');
}

async function openWorkspaceDocument(relativePath) {
  const uri = vscode.Uri.file(path.join(workspaceRootPath(), relativePath));
  const document = await vscode.workspace.openTextDocument(uri);
  await vscode.window.showTextDocument(document, { preview: false });
  return document;
}

async function waitForHover(document, needle, expectedFragments, occurrence = 0) {
  const position = positionForNeedle(document, needle, occurrence);
  const deadline = Date.now() + HOVER_TIMEOUT_MS;
  let lastText = '';
  while (Date.now() < deadline) {
    const hovers = await vscode.commands.executeCommand(
      'vscode.executeHoverProvider',
      document.uri,
      position
    );
    lastText = (hovers || []).map(hoverText).join('\n---\n');
    if (expectedFragments.every((fragment) => lastText.includes(fragment))) {
      return lastText;
    }
    await sleep(HOVER_POLL_MS);
  }
  assert.fail(
    `hover for ${needle} did not contain ${JSON.stringify(expectedFragments)}\nLast hover:\n${lastText}`
  );
}

function completionItems(result) {
  if (!result) {
    return [];
  }
  if (Array.isArray(result)) {
    return result;
  }
  return result.items || [];
}

async function waitForCompletionLabels(document, position, expectedLabels) {
  const deadline = Date.now() + COMPLETION_TIMEOUT_MS;
  let lastLabels = [];
  while (Date.now() < deadline) {
    const result = await vscode.commands.executeCommand(
      'vscode.executeCompletionItemProvider',
      document.uri,
      position
    );
    lastLabels = completionItems(result).map((item) => item.label);
    if (expectedLabels.every((label) => lastLabels.includes(label))) {
      return lastLabels;
    }
    await sleep(COMPLETION_POLL_MS);
  }

  assert.fail(
    `completion did not contain ${JSON.stringify(expectedLabels)}\nLast labels:\n${JSON.stringify(
      lastLabels,
      null,
      2
    )}`
  );
}

async function waitForDiagnostics(uri, predicate, description) {
  const deadline = Date.now() + DIAGNOSTIC_TIMEOUT_MS;
  let lastDiagnostics = [];
  while (Date.now() < deadline) {
    lastDiagnostics = vscode.languages.getDiagnostics(uri);
    if (predicate(lastDiagnostics)) {
      return lastDiagnostics;
    }
    await sleep(DIAGNOSTIC_POLL_MS);
  }

  assert.fail(
    `diagnostics did not satisfy ${description}\nLast diagnostics:\n${JSON.stringify(
      lastDiagnostics,
      null,
      2
    )}`
  );
}

async function configureExtension() {
  const serverPath = process.env.AHFL_VSCODE_TEST_SERVER;
  assert.ok(serverPath, 'AHFL_VSCODE_TEST_SERVER must be set');
  assert.ok(fs.existsSync(serverPath), `AHFL language server does not exist: ${serverPath}`);

  const config = vscode.workspace.getConfiguration('ahfl');
  await config.update('serverPath', serverPath, vscode.ConfigurationTarget.Global);
  await config.update('hover.detailLevel', 'standard', vscode.ConfigurationTarget.Global);
  await config.update('hover.markupKind', 'auto', vscode.ConfigurationTarget.Global);
  await config.update('hover.maxFacts', 3, vscode.ConfigurationTarget.Global);
  await config.update('hover.showSource', false, vscode.ConfigurationTarget.Global);

  const extension = vscode.extensions.getExtension('ahfl.ahfl-language');
  assert.ok(extension, 'AHFL extension under test is not registered');
  await extension.activate();
}

async function runHoverRegression() {
  const agents = await openWorkspaceDocument('lib/agents.ahfl');
  await waitForHover(agents, 'input:', ['input: lib::types::Request', 'Input schema for']);
  await waitForHover(agents, 'states:', ['states: [Init, Done]', 'Defines 2 states']);

  const types = await openWorkspaceDocument('lib/types.ahfl');
  await waitForHover(types, 'RequestAlias =', [
    'type lib::types::RequestAlias = lib::types::Request',
    'type alias',
  ]);
  await waitForHover(types, 'value: String', ['value: String', 'Field of']);

  const workflow = await openWorkspaceDocument('app/main.ahfl');
  await waitForHover(workflow, 'run: agents::AliasAgent', [
    'node run: agents::AliasAgent',
    'workflow node',
  ]);
}

async function runCompletionRegression() {
  const workflow = await openWorkspaceDocument('app/main.ahfl');
  await waitForCompletionLabels(workflow, positionAfterNeedle(workflow, 'input.'), ['value']);
}

async function runRenameRegression() {
  const tempDir = fs.mkdtempSync(path.join(workspaceRootPath(), 'ahfl-vscode-rename-'));
  const tempFile = path.join(tempDir, 'rename.ahfl');
  const source = [
    'module scratch::rename;',
    '',
    'struct Msg {',
    '    value: String;',
    '}',
    '',
    'struct Envelope {',
    '    payload: Msg;',
    '}',
    '',
  ].join('\n');

  try {
    writePackageManifest(tempDir, 'vscode-rename', 'scratch', ['rename'], 'rename.ahfl');
    writeTextFile(tempFile, source);
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(tempFile));
    await vscode.window.showTextDocument(document, { preview: false });

    const edit = await vscode.commands.executeCommand(
      'vscode.executeDocumentRenameProvider',
      document.uri,
      positionForNeedle(document, 'Msg'),
      'Message'
    );
    assert.ok(edit, 'expected AHFL rename provider to return a workspace edit');

    const applied = await vscode.workspace.applyEdit(edit);
    assert.strictEqual(applied, true, 'expected AHFL rename workspace edit to apply');
    await document.save();

    const renamed = document.getText();
    assert.ok(renamed.includes('struct Message {'), 'expected renamed struct declaration');
    assert.ok(renamed.includes('payload: Message;'), 'expected renamed struct reference');
    assert.strictEqual(renamed.includes('struct Msg {'), false, 'old struct declaration remains');
    assert.strictEqual(renamed.includes('payload: Msg;'), false, 'old struct reference remains');
  } finally {
    await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
    await sleep(500);
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
}

async function replaceDocumentText(document, content) {
  const edit = new vscode.WorkspaceEdit();
  const fullRange = new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length));
  edit.replace(document.uri, fullRange, content);
  const applied = await vscode.workspace.applyEdit(edit);
  assert.strictEqual(applied, true, 'expected VS Code workspace edit to apply');
  await document.save();
}

async function runDiagnosticsRegression() {
  const tempDir = fs.mkdtempSync(path.join(workspaceRootPath(), 'ahfl-vscode-diagnostics-'));
  const tempFile = path.join(tempDir, 'broken.ahfl');
  const brokenSource = 'module scratch::broken;\n\nstruct Broken {\n    value: Missing;\n}\n';
  const fixedSource = 'module scratch::broken;\n\nstruct Broken {\n    value: String;\n}\n';

  try {
    writePackageManifest(tempDir, 'vscode-diagnostics', 'scratch', ['broken'], 'broken.ahfl');
    writeTextFile(tempFile, brokenSource);
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(tempFile));
    await vscode.window.showTextDocument(document, { preview: false });

    const diagnostics = await waitForDiagnostics(
      document.uri,
      (items) => items.some((diagnostic) => diagnostic.severity === vscode.DiagnosticSeverity.Error),
      'at least one error diagnostic for a broken AHFL file'
    );
    recordDiagnosticsSnapshot('broken-file.error-published', document.uri, diagnostics);
    assert.ok(
      diagnostics.some((diagnostic) => diagnostic.source === 'ahfl'),
      'expected diagnostics to be published by AHFL language server'
    );

    await replaceDocumentText(document, fixedSource);
    const fixedDiagnostics = await waitForDiagnostics(
      document.uri,
      (items) => items.length === 0,
      'empty diagnostics after fixing the AHFL file'
    );
    recordDiagnosticsSnapshot('broken-file.fixed-cleared', document.uri, fixedDiagnostics);
  } finally {
    await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
}

async function runWatchedFilesRegression() {
  const tempDir = fs.mkdtempSync(path.join(workspaceRootPath(), 'ahfl-vscode-watch-'));
  const mainFile = path.join(tempDir, 'main.ahfl');
  const typesFile = path.join(tempDir, 'types.ahfl');
  const mainSource = [
    'module app::main;',
    'import app::types as types;',
    '',
    'struct Use {',
    '    payload: types::Msg;',
    '}',
    '',
  ].join('\n');
  const validTypesSource = [
    'module app::types;',
    '',
    'struct Msg {',
    '    value: String;',
    '}',
    '',
  ].join('\n');
  const missingTypeSource = ['module app::types;', '', 'struct Other {', '    value: String;', '}', ''].join(
    '\n'
  );

  try {
    writePackageManifest(tempDir, 'vscode-watch', 'app', ['main', 'types'], 'main.ahfl');
    writeTextFile(mainFile, mainSource);
    writeTextFile(typesFile, validTypesSource);

    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(mainFile));
    await vscode.window.showTextDocument(document, { preview: false });
    const beforeDiagnostics = await waitForDiagnostics(
      document.uri,
      (items) => items.length === 0,
      'empty diagnostics before imported file changes'
    );
    recordDiagnosticsSnapshot('watched-files.before-import-change', document.uri, beforeDiagnostics);

    await vscode.workspace.fs.writeFile(vscode.Uri.file(typesFile), Buffer.from(missingTypeSource, 'utf8'));
    const afterDiagnostics = await waitForDiagnostics(
      document.uri,
      (items) =>
        items.some(
          (diagnostic) =>
            diagnostic.severity === vscode.DiagnosticSeverity.Error &&
            diagnostic.source === 'ahfl' &&
            (diagnosticCodeText(diagnostic).startsWith('resolve.') ||
              diagnostic.message.includes('types::Msg') ||
              diagnostic.message.includes('Msg'))
      ),
      'AHFL resolve diagnostic after an unopened imported file removes Msg'
    );
    recordDiagnosticsSnapshot('watched-files.after-import-change', document.uri, afterDiagnostics);
  } finally {
    await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
}

async function run() {
  await configureExtension();
  await runHoverRegression();
  await runCompletionRegression();
  await runRenameRegression();
  await runDiagnosticsRegression();
  await runWatchedFilesRegression();
  writeDiagnosticsTranscript();
}

module.exports = { run };
