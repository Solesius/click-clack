import { TestBed } from '@angular/core/testing'
import { Subject } from 'rxjs'
import { HubStateService } from './hub-state.service'
import { WsService } from './ws.service'
import { mockWsService, type MockWsService, makeClack, makePin } from '../../testing/mocks'
import type { PinEntry, WsMessage } from '../models/types'

describe('HubStateService', () => {
  let ws: MockWsService
  let service: HubStateService

  beforeEach(() => {
    localStorage.clear()
    ws = mockWsService()
    TestBed.configureTestingModule({
      providers: [HubStateService, { provide: WsService, useValue: ws }],
    })
    service = TestBed.inject(HubStateService)
  })

  describe('operatorId', () => {
    it('should default to "operator" when localStorage is empty', () => {
      expect(service.operatorId()).toBe('operator')
    })

    it('should persist operator id to localStorage on set', () => {
      service.setOperatorId('khalil')
      expect(service.operatorId()).toBe('khalil')
      expect(localStorage.getItem('cc.operator_id')).toBe('khalil')
    })

    it('should fall back to "operator" when set with empty string', () => {
      service.setOperatorId('')
      expect(service.operatorId()).toBe('operator')
    })
  })

  describe('init()', () => {
    it('should connect and subscribe to every view when ws opens', () => {
      spyOn(service, 'refreshPins').and.returnValue(Promise.resolve())
      service.init()
      expect(ws.connect).toHaveBeenCalled()
      ws.open$.next()
      expect(ws.subscribe).toHaveBeenCalledWith('timeline')
      expect(ws.subscribe).toHaveBeenCalledWith('tasks')
      expect(ws.subscribe).toHaveBeenCalledWith('agents')
      expect(ws.subscribe).toHaveBeenCalledWith('hitl')
    })
  })

  describe('handleMessage (via ws stream)', () => {
    it('should append clacks and advance lastEpoch', () => {
      const clack = makeClack({ epoch: 42 })
      ws.message$.next({ type: 'clack', data: clack } as WsMessage)
      expect(service.timeline()).toEqual([clack])
      expect(service.lastEpoch()).toBe(42)
    })

    it('should replace timeline on snapshot', () => {
      const c1 = makeClack({ epoch: 1 })
      const c2 = makeClack({ epoch: 2 })
      ws.message$.next({ type: 'snapshot', view: 'timeline', data: [c1, c2] })
      expect(service.timeline().length).toBe(2)
      expect(service.lastEpoch()).toBe(2)
    })

    it('should trigger refreshPins on pin-governance OBSERVE clack', () => {
      const spy = spyOn(service, 'refreshPins').and.returnValue(Promise.resolve())
      ws.message$.next({
        type: 'clack',
        data: makeClack({ epoch: 10, verb: 'Observe', subject: 'pin:5' }),
      })
      expect(spy).toHaveBeenCalled()
    })

    it('should trigger refreshPins on pin_override DIRECT clack', () => {
      const spy = spyOn(service, 'refreshPins').and.returnValue(Promise.resolve())
      ws.message$.next({
        type: 'clack',
        data: makeClack({ epoch: 11, verb: 'Direct', subject: 'pin_override:5' }),
      })
      expect(spy).toHaveBeenCalled()
    })
  })

  describe('callTool()', () => {
    it('should POST with operator identity auto-injected', async () => {
      const fetchSpy = spyOn(window, 'fetch').and.resolveTo(
        new Response(JSON.stringify({ ok: true }), { status: 200 }),
      )
      service.setOperatorId('khalil')
      const out = await service.callTool('cc.query_tasks', { limit: 10 })
      expect(out).toEqual({ ok: true } as unknown as object)
      expect(fetchSpy).toHaveBeenCalledTimes(1)
      const [url, init] = fetchSpy.calls.mostRecent().args
      expect(url).toBe('/api/v1/mcp/call')
      const body = JSON.parse(((init as RequestInit).body as string) || '{}')
      expect(body.tool).toBe('cc.query_tasks')
      expect(body.agent_id).toBe('khalil')
      expect(body.args).toEqual({ as_agent: 'khalil', limit: 10 })
    })

    it('should throw on non-2xx responses', async () => {
      spyOn(window, 'fetch').and.resolveTo(
        new Response('fail', { status: 500 }),
      )
      await expectAsync(service.callTool('cc.query_tasks')).toBeRejectedWithError(
        /mcp cc.query_tasks http 500/,
      )
    })
  })

  describe('postClack()', () => {
    it('should parse string payload as JSON when valid', async () => {
      const spy = spyOn(service, 'callTool').and.resolveTo({
        epoch: 5,
        task_id: 't-x',
      })
      await service.postClack({ verb: 'ANNOUNCE', payload: '{"a":1}' })
      expect(spy).toHaveBeenCalled()
      const args = spy.calls.mostRecent().args[1] as Record<string, unknown>
      expect(args['payload']).toEqual({ a: 1 })
    })

    it('should wrap unparseable string payload as { note }', async () => {
      const spy = spyOn(service, 'callTool').and.resolveTo({ epoch: 6, task_id: 't' })
      await service.postClack({ verb: 'ANNOUNCE', payload: 'not json' })
      const args = spy.calls.mostRecent().args[1] as Record<string, unknown>
      expect(args['payload']).toEqual({ note: 'not json' })
    })

    it('should default flags and parent to 0', async () => {
      const spy = spyOn(service, 'callTool').and.resolveTo({ epoch: 7, task_id: 't' })
      await service.postClack({ verb: 'CLAIM' })
      const args = spy.calls.mostRecent().args[1] as Record<string, unknown>
      expect(args['flags']).toBe(0)
      expect(args['parent']).toBe(0)
    })
  })

  describe('pin operations', () => {
    it('refreshPins should populate pins signal from callTool result', async () => {
      const entries: PinEntry[] = [makePin({ epoch: 1, pinned: true, votes: 2 })]
      spyOn(service, 'callTool').and.resolveTo(entries)
      await service.refreshPins()
      expect(service.pins()).toEqual(entries)
      expect(service.pinnedEpochs().has(1)).toBeTrue()
    })

    it('refreshPins should coerce non-array response to []', async () => {
      spyOn(service, 'callTool').and.resolveTo({ error: 'bad' } as unknown)
      await service.refreshPins()
      expect(service.pins()).toEqual([])
    })

    it('votePin should call cc.vote_pin and then refresh', async () => {
      const call = spyOn(service, 'callTool').and.resolveTo({})
      const refresh = spyOn(service, 'refreshPins').and.resolveTo()
      await service.votePin(12, false)
      expect(call).toHaveBeenCalledWith('cc.vote_pin', { epoch: 12, unvote: false })
      expect(refresh).toHaveBeenCalled()
    })

    it('pinOverride should pass through explicit null', async () => {
      const call = spyOn(service, 'callTool').and.resolveTo({})
      spyOn(service, 'refreshPins').and.resolveTo()
      await service.pinOverride(12, null)
      expect(call).toHaveBeenCalledWith('cc.pin_override', { epoch: 12, pinned: null })
    })
  })
})
