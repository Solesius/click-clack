#!/usr/bin/env bun
// End-to-end smoke example: open a hub, post a clack, claim it, finish it.

import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { ClickClackHub } from '../src/index.ts'

const root = mkdtempSync(join(tmpdir(), 'click-clack-bun-'))
const hub = ClickClackHub.open({
  walPath:   join(root, 'wal'),
  viewsPath: join(root, 'views'),
})

try {
  console.log('native version:', ClickClackHub.nativeVersion())

  const tools = hub.listTools()
  console.log(`registered tools: ${tools.length}`)

  const announced = hub.postClack({
    verb: 'ANNOUNCE',
    taskId: 'demo-1',
    subject: 'Write release notes',
    payload: { effort: 'small' },
    as: 'alice',
  })
  console.log('announced:', announced)

  const claim = hub.claimTask('demo-1', 'bob')
  console.log('claimed:', claim)

  hub.reportProgress('demo-1', 50, 'halfway there', 'bob')
  const done = hub.completeTask('demo-1', 'all set', 'bob')
  console.log('completed:', done)

  const task = hub.queryTask('demo-1')
  console.log('final task state:', task)
} finally {
  hub.close()
  rmSync(root, { recursive: true, force: true })
}
