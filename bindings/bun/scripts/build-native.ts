#!/usr/bin/env bun
// Build the native libclickclack library via cmake and copy it into
// ./native/ so the package ships a self-contained prebuild.

import { mkdirSync, copyFileSync, existsSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { suffix } from 'bun:ffi'
import { $ } from 'bun'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..', '..', '..')
const BUILD = join(REPO, 'build-ffi')
const NATIVE_DIR = resolve(HERE, '..', 'native')
const LIB = `libclickclack.${suffix}`

const jobs = Number(process.env.JOBS ?? navigator.hardwareConcurrency ?? 4)

console.log(`[build-native] repo=${REPO}`)
console.log(`[build-native] configuring cmake -> ${BUILD}`)

await $`cmake -S ${REPO} -B ${BUILD} -DCC_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release`.quiet()

console.log(`[build-native] building click_clack_ffi (jobs=${jobs})`)
await $`cmake --build ${BUILD} -j ${jobs} -t click_clack_ffi`

const built = join(BUILD, LIB)
if (!existsSync(built)) {
  console.error(`[build-native] expected ${built} to exist`)
  process.exit(1)
}

mkdirSync(NATIVE_DIR, { recursive: true })
const dest = join(NATIVE_DIR, LIB)
copyFileSync(built, dest)
console.log(`[build-native] copied ${built} -> ${dest}`)
