const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const HOVER_TIMEOUT_MS = 15_000;
const HOVER_POLL_MS = 250;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
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

async function openWorkspaceDocument(relativePath) {
  const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
  assert.ok(workspaceFolder, 'expected hover test workspace folder');
  const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, relativePath));
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

async function run() {
  await configureExtension();

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
    'Workflow node in',
  ]);
}

module.exports = { run };
