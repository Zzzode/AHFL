const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const esbuild = require('esbuild');

const extensionRoot = path.resolve(__dirname, '..');

function bundleDebugModule() {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'ahfl-vscode-debug-'));
  const outfile = path.join(tempRoot, 'debug.cjs');
  esbuild.buildSync({
    entryPoints: [path.join(extensionRoot, 'src', 'debug.ts')],
    bundle: true,
    platform: 'node',
    format: 'cjs',
    target: 'node18',
    outfile,
  });
  return { tempRoot, module: require(outfile) };
}

function assertResolution(debug) {
  // Explicit configuration wins over everything, untouched (trimmed).
  assert.strictEqual(
    debug.resolveDebugAdapterCommand({
      configuredPath: '  /opt/ahfl/ahfl-dap  ',
      bundledPath: '/ext/server/ahfl-dap',
      fileExists: () => true,
    }),
    '/opt/ahfl/ahfl-dap',
    'configured path must take precedence and be trimmed'
  );

  // Empty configuration falls back to the bundled binary when it exists.
  assert.strictEqual(
    debug.resolveDebugAdapterCommand({
      configuredPath: '',
      bundledPath: '/ext/server/ahfl-dap',
      fileExists: (candidate) => candidate === '/ext/server/ahfl-dap',
    }),
    '/ext/server/ahfl-dap',
    'bundled adapter must be used when present and no path configured'
  );

  // No configuration and no bundled binary falls back to PATH lookup.
  assert.strictEqual(
    debug.resolveDebugAdapterCommand({
      configuredPath: '   ',
      bundledPath: '/ext/server/ahfl-dap',
      fileExists: () => false,
    }),
    'ahfl-dap',
    'must fall back to PATH lookup when nothing else resolves'
  );

  assert.strictEqual(debug.debugAdapterExecutableName('linux'), 'ahfl-dap');
  assert.strictEqual(debug.debugAdapterExecutableName('darwin'), 'ahfl-dap');
  assert.strictEqual(debug.debugAdapterExecutableName('win32'), 'ahfl-dap.exe');

  assert.strictEqual(
    debug.bundledDebugAdapterPath('/ext', 'linux'),
    path.join('/ext', 'server', 'ahfl-dap')
  );
  assert.strictEqual(
    debug.bundledDebugAdapterPath('/ext', 'win32'),
    path.join('/ext', 'server', 'ahfl-dap.exe')
  );
}

function assertPackageContribution() {
  const manifest = JSON.parse(fs.readFileSync(path.join(extensionRoot, 'package.json'), 'utf8'));

  const activationEvents = manifest.activationEvents || [];
  assert.ok(
    activationEvents.includes('onDebug:ahfl'),
    'package.json must activate on onDebug:ahfl'
  );

  const debuggers = manifest.contributes && manifest.contributes.debuggers;
  assert.ok(Array.isArray(debuggers) && debuggers.length === 1, 'expected one debuggers contribution');

  const debugger0 = debuggers[0];
  assert.strictEqual(debugger0.type, 'ahfl', 'debugger type must be ahfl');
  assert.strictEqual(debugger0.label, 'AHFL Debug', 'debugger label must be AHFL Debug');

  // The adapter is launched as a stdio executable via the extension's factory,
  // so the manifest must NOT hard-code a `program` (that would bypass the
  // configurable / bundled path resolution). This is the key spec detail for
  // a factory-based adapter.
  assert.ok(!('program' in debugger0), 'debugger must not hard-code a program path');

  const launch = debugger0.configurationAttributes && debugger0.configurationAttributes.launch;
  assert.ok(launch && launch.properties, 'launch configurationAttributes must define properties');
  assert.ok(launch.properties.program, 'launch must expose a program attribute');
  assert.strictEqual(launch.properties.program.type, 'string');
  assert.ok(launch.properties.workflow, 'launch must expose a workflow attribute');
  assert.strictEqual(launch.properties.workflow.type, 'string');
  assert.ok(
    Array.isArray(launch.required) && launch.required.includes('program'),
    'launch must mark program as required'
  );

  assert.ok(
    Array.isArray(debugger0.initialConfigurations) && debugger0.initialConfigurations.length >= 1,
    'debugger must ship initialConfigurations'
  );
  for (const config of debugger0.initialConfigurations) {
    assert.strictEqual(config.type, 'ahfl');
    assert.strictEqual(config.request, 'launch');
    assert.ok(typeof config.name === 'string' && config.name.length > 0);
    assert.ok(typeof config.program === 'string' && config.program.length > 0);
  }

  assert.ok(
    Array.isArray(debugger0.configurationSnippets) && debugger0.configurationSnippets.length >= 1,
    'debugger must ship configurationSnippets'
  );
  for (const snippet of debugger0.configurationSnippets) {
    assert.ok(typeof snippet.label === 'string' && snippet.label.length > 0);
    assert.ok(snippet.body && snippet.body.type === 'ahfl' && snippet.body.request === 'launch');
  }

  const breakpoints = manifest.contributes && manifest.contributes.breakpoints;
  assert.ok(
    Array.isArray(breakpoints) && breakpoints.some((entry) => entry.language === 'ahfl'),
    'package.json must enable breakpoints for the ahfl language'
  );

  const properties = manifest.contributes.configuration.properties;
  assert.ok(properties['ahfl.debugAdapterPath'], 'ahfl.debugAdapterPath setting must exist');
  assert.strictEqual(properties['ahfl.debugAdapterPath'].type, 'string');
  assert.ok(properties['ahfl.debugAdapterArgs'], 'ahfl.debugAdapterArgs setting must exist');
  assert.strictEqual(properties['ahfl.debugAdapterArgs'].type, 'array');
}

function main() {
  const { tempRoot, module: debug } = bundleDebugModule();
  try {
    assertResolution(debug);
    assertPackageContribution();
    console.log('Verified AHFL VS Code debug adapter contribution and resolution');
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
