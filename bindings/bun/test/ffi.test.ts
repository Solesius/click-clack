import { describe, it, expect, beforeAll, afterAll } from 'bun:test'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { ClickClackHub } from '../src/index.ts'

describe('ClickClackHub', () => {
  let root: string
  let hub: ClickClackHub

  beforeAll(() => {
    root = mkdtempSync(join(tmpdir(), 'cc-bun-test-'))
    hub = ClickClackHub.open({
      walPath:   join(root, 'wal'),
      viewsPath: join(root, 'views'),
    })
  })

  afterAll(() => {
    hub.close()
    rmSync(root, { recursive: true, force: true })
  })

  it('exposes a non-empty native version', () => {
    expect(ClickClackHub.nativeVersion()).not.toBe('')
  })

  it('lists registered MCP tools', () => {
    const tools = hub.listTools()
    expect(tools.length).toBeGreaterThan(5)
    expect(tools.some(t => t.name === 'cc.post_clack')).toBe(true)
  })

  it('runs a full announce → claim → complete lifecycle', () => {
    const task = `task-${Date.now()}`
    const announced = hub.postClack({
      verb: 'ANNOUNCE',
      taskId: task,
      subject: 'bun ffi e2e',
      as: 'alice',
    }) as { epoch: number; task_id: string }
    expect(announced.epoch).toBeGreaterThan(0)
    expect(announced.task_id).toBe(task)

    const claim = hub.claimTask(task, 'bob') as { epoch: number }
    expect(claim.epoch).toBeGreaterThan(announced.epoch)

    hub.reportProgress(task, 42, 'working', 'bob')
    hub.completeTask(task, 'done', 'bob')

    const state = hub.queryTask(task) as { status: string }
    expect(state.status).toBe('completed')
  })

  it('returns a structured error object for unknown tools', () => {
    const res = hub.call<{ error?: string; tool?: string }>('cc.definitely_not_a_tool')
    expect(res.error).toBe('unknown_tool')
    expect(res.tool).toBe('cc.definitely_not_a_tool')
  })
})
