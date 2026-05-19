#!/usr/bin/env node
// ---------------------------------------------------------------------------
// logos-standalone-app integration tests — counter_qml plugin
//
// Verifies the core standalone-app functionality:
//   - Loading a ui_qml plugin and rendering its QML
//   - User interaction via the QML Inspector bridge
//
// Usage:
//   node test-counter-qml.mjs                      # run (app must be running)
//   node test-counter-qml.mjs --ci <launch-script> # CI mode: launch app, test, kill
//
// Set LOGOS_QT_MCP to override the framework path (nix builds set this automatically).
// Default: ../result-mcp (built via: nix build ..#logos-qt-mcp -o result-mcp)
// ---------------------------------------------------------------------------

import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const qtMcpRoot = process.env.LOGOS_QT_MCP || resolve(__dirname, "../result-mcp");
const { test, run } = await import(resolve(qtMcpRoot, "test-framework/framework.mjs"));

// counter_qml renders: title "QML Counter", count display starting at 0,
// and a button labelled "Increment" that triggers root.count++.
//
// Tests run sequentially in a single app instance, so state accumulates:
//   test 1 → count stays at 0
//   test 2 → clicks once, count reaches 1
//   test 3 → clicks once more, count reaches 2

test("counter_qml: plugin loads and shows initial state", async (app) => {
  // Wait for the plugin to initialise and paint before asserting.
  await app.waitFor(
    async () => { await app.expectTexts(["QML Counter", "0"]); },
    { timeout: 10000, interval: 500, description: "counter_qml plugin to load and render" }
  );
});

test("counter_qml: first increment reaches 1", async (app) => {
  await app.click("Increment");

  // Use waitFor — click-to-paint latency varies between Linux (no QCocoa)
  // and macOS (QCocoa in dispatch path even under -platform offscreen).
  await app.waitFor(
    async () => { await app.expectTexts(["1"]); },
    { timeout: 5000, interval: 200, description: "count to reach 1" }
  );
});

test("counter_qml: second increment reaches 2", async (app) => {
  await app.click("Increment");

  await app.waitFor(
    async () => { await app.expectTexts(["2"]); },
    { timeout: 5000, interval: 200, description: "count to reach 2" }
  );
});

run();
