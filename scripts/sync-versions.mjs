/**
 * Syncs the new version into all package manifests.
 * Called by semantic-release via @semantic-release/exec.
 *
 * Usage: node scripts/sync-versions.mjs 1.2.3
 */

import { readFileSync, writeFileSync } from 'fs';
import { resolve } from 'path';

const version = process.argv[2];
if (!version) {
  console.error('Usage: node sync-versions.mjs <version>');
  process.exit(1);
}

console.log(`Syncing version ${version} across all packages...`);

// ── npm (package.json) ──────────────────────────────────────
function updateJson(filePath) {
  const abs = resolve(filePath);
  const pkg = JSON.parse(readFileSync(abs, 'utf8'));
  pkg.version = version;
  writeFileSync(abs, JSON.stringify(pkg, null, 2) + '\n');
  console.log(`  updated ${filePath}`);
}

updateJson('bindings/wasm/package.json');

// ── Rust (Cargo.toml) ───────────────────────────────────────
function updateToml(filePath) {
  const abs = resolve(filePath);
  let content = readFileSync(abs, 'utf8');
  content = content.replace(
    /^version\s*=\s*"[^"]*"/m,
    `version = "${version}"`
  );
  writeFileSync(abs, content);
  console.log(`  updated ${filePath}`);
}

updateToml('bindings/rust/protocoll-sys/Cargo.toml');
updateToml('bindings/rust/protocoll/Cargo.toml');

// ── Python (pyproject.toml) ─────────────────────────────────
updateToml('bindings/python/pyproject.toml');

// ── .NET (csproj) ──────────────────────────────────────────
function updateCsproj(filePath) {
  const abs = resolve(filePath);
  let content = readFileSync(abs, 'utf8');
  content = content.replace(
    /<Version>[^<]*<\/Version>/,
    `<Version>${version}</Version>`
  );
  writeFileSync(abs, content);
  console.log(`  updated ${filePath}`);
}

updateCsproj('bindings/dotnet/src/RMNunes.Rom/RMNunes.Rom.csproj');

console.log('Done.');
