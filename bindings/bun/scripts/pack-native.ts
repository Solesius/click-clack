#!/usr/bin/env bun
// prepack hook — make sure the tarball that gets published contains
// a prebuilt libclickclack for the host running `bun publish` / `npm pack`.
//
// CI publishes from a matrix job per platform, each one producing a
// single-host tarball. In practice we prefer to run `bun publish`
// after `scripts/collect-prebuilds.ts` has gathered all platform
// artifacts under ./native/, but this hook guarantees at minimum
// the current host's lib is always packed.

import { existsSync, mkdirSync, copyFileSync, statSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { suffix } from 'bun:ffi'
import { $ } from 'bun'

const HERE = dirname(fileURLToPath(import.meta.url))
const PKG  = resolve(HERE, '..')
const REPO = resolve(PKG, '..', '..')
const LIB  = `libclickclack.${suffix}`
const PLAT = `${process.platform}-${process.arch}`
const DEST_DIR = join(PKG, 'native', PLAT)
const DEST     = join(DEST_DIR, LIB)

function ageSeconds(p: string): number {
  return (Date.now() - statSync(p).mtimeMs) / 1000
}

async function ensureBuild(): Promise<string> {
  // Respect an env override for pre-built tarballs (e.g. CI collected).
  const override = process.env.CLICK_CLACK_PREBUILD
  if (override && existsSync(override)) return override

  // If a host prebuild was already staged (CI download-artifact path),
  // nothing to do — just keep it.
  if (existsSync(DEST)) {
    console.log(`[pack-native] ${PLAT}: already staged at ${DEST}`)
    return DEST
  }

  const candidates = [
    join(REPO, 'build',     LIB),
    join(REPO, 'build-ffi', LIB),
  ]
  for (const c of candidates) if (existsSync(c)) return c

  console.log('[pack-native] no prebuild found; running cmake')
  const buildDir = join(REPO, 'build-ffi')
  await $`cmake -S ${REPO} -B ${buildDir} -DCC_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release`.quiet()
  await $`cmake --build ${buildDir} -j -t click_clack_ffi`
  const out = join(buildDir, LIB)
  if (!existsSync(out)) throw new Error(`build produced no ${out}`)
  return out
}

const src = await ensureBuild()
if (src !== DEST) {
  mkdirSync(DEST_DIR, { recursive: true })
  copyFileSync(src, DEST)
}
console.log(`[pack-native] ${PLAT}: ${src} -> ${DEST} (age ${ageSeconds(DEST).toFixed(0)}s)`)
