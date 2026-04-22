import { TestBed } from '@angular/core/testing'
import { WsService } from './ws.service'

type Listener = (ev: unknown) => void
class FakeSocket {
  static instances: FakeSocket[] = []
  static readonly OPEN = 1
  readyState = 0
  sent: string[] = []
  onopen: Listener | null = null
  onclose: Listener | null = null
  onerror: Listener | null = null
  onmessage: Listener | null = null
  constructor(public url: string) {
    FakeSocket.instances.push(this)
  }
  send(data: string): void { this.sent.push(data) }
  close(): void { this.onclose?.({}) }
  open(): void { this.readyState = FakeSocket.OPEN; this.onopen?.({}) }
  receive(data: unknown): void { this.onmessage?.({ data: JSON.stringify(data) }) }
  receiveRaw(raw: string): void { this.onmessage?.({ data: raw }) }
}

describe('WsService', () => {
  let service: WsService
  let originalWS: typeof WebSocket

  beforeEach(() => {
    FakeSocket.instances = []
    originalWS = (globalThis as { WebSocket: typeof WebSocket }).WebSocket
    ;(globalThis as unknown as { WebSocket: unknown }).WebSocket =
      FakeSocket as unknown as typeof WebSocket
    jasmine.clock().install()
    TestBed.configureTestingModule({ providers: [WsService] })
    service = TestBed.inject(WsService)
  })

  afterEach(() => {
    jasmine.clock().uninstall()
    ;(globalThis as unknown as { WebSocket: unknown }).WebSocket = originalWS
  })

  it('is disconnected initially', () => {
    expect(service.connected()).toBeFalse()
    expect(service.statusText()).toBe('Disconnected')
  })

  it('send() is a no-op when socket is not open', () => {
    expect(() => service.send('post', { foo: 1 })).not.toThrow()
  })

  it('connect() opens a socket and flips connected on open', () => {
    const openSpy = jasmine.createSpy('open')
    service.open$.subscribe(() => openSpy())
    service.connect('ws://test/ws')
    const sock = FakeSocket.instances.at(-1)!
    sock.open()
    expect(service.connected()).toBeTrue()
    expect(service.statusText()).toBe('Connected')
    expect(openSpy).toHaveBeenCalledTimes(1)
  })

  it('subscribes, unsubscribes and posts clicks via send()', () => {
    service.connect('ws://test/ws')
    const sock = FakeSocket.instances.at(-1)!
    sock.open()

    service.subscribe('timeline')
    service.unsubscribe('timeline')
    service.postClick({ verb: 'Announce', agent_id: 'a', task_id: 't' } as never)

    const parsed = sock.sent.map(s => JSON.parse(s))
    expect(parsed[0]).toEqual({ action: 'subscribe', view: 'timeline' })
    expect(parsed[1]).toEqual({ action: 'unsubscribe', view: 'timeline' })
    expect(parsed[2].action).toBe('post')
    expect(parsed[2].click.verb).toBe('Announce')
  })

  it('routes parsed messages through message$', () => {
    const got: unknown[] = []
    service.message$.subscribe(m => got.push(m))
    service.connect('ws://test/ws')
    const sock = FakeSocket.instances.at(-1)!
    sock.open()
    sock.receive({ type: 'ack', epoch: 7 })
    expect(got.length).toBe(1)
    expect((got[0] as { type: string }).type).toBe('ack')
  })

  it('silently drops unparseable frames', () => {
    const warn = spyOn(console, 'warn')
    const got: unknown[] = []
    service.message$.subscribe(m => got.push(m))
    service.connect('ws://test/ws')
    const sock = FakeSocket.instances.at(-1)!
    sock.open()
    sock.receiveRaw('not json')
    expect(got.length).toBe(0)
    expect(warn).toHaveBeenCalled()
  })

  it('schedules a reconnect on close', () => {
    service.connect('ws://test/ws')
    const sock = FakeSocket.instances.at(-1)!
    sock.open()
    expect(service.connected()).toBeTrue()
    sock.close()
    expect(service.connected()).toBeFalse()
    jasmine.clock().tick(2001)
    expect(FakeSocket.instances.length).toBe(2)
  })

  it('disconnect() closes the socket and clears connected state', () => {
    service.connect('ws://test/ws')
    const sock = FakeSocket.instances.at(-1)!
    sock.open()
    service.disconnect()
    expect(service.connected()).toBeFalse()
  })

  // ── F-04: proto-pollution-safe WS parsing ──────────────────

  describe('F-04 — proto-pollution defence', () => {
    it('should strip __proto__ keys from inbound JSON (reviver)', () => {
      const got: unknown[] = []
      service.message$.subscribe(m => got.push(m))
      service.connect('ws://test/ws')
      const sock = FakeSocket.instances.at(-1)!
      sock.open()
      // Send a frame containing __proto__ injection.
      sock.receiveRaw('{"type":"clack","__proto__":{"evil":true},"data":{}}')
      expect(got.length).toBe(1)
      // __proto__ must not have reached Object.prototype
      expect((Object.prototype as Record<string, unknown>)['evil']).toBeUndefined()
    })

    it('should strip constructor and prototype keys from inbound JSON', () => {
      const got: unknown[] = []
      service.message$.subscribe(m => got.push(m))
      service.connect('ws://test/ws')
      const sock = FakeSocket.instances.at(-1)!
      sock.open()
      sock.receiveRaw('{"type":"ack","constructor":{"prototype":{"polluted":true}}}')
      expect(got.length).toBe(1)
      // polluted must not have reached Object.prototype
      expect((Object.prototype as Record<string, unknown>)['polluted']).toBeUndefined()
    })

    it('should build outbound frame without spreading untrusted payload', () => {
      service.connect('ws://test/ws')
      const sock = FakeSocket.instances.at(-1)!
      sock.open()
      // A payload containing __proto__ should not override the action field.
      service.send('post', {
        __proto__: { evil: true },
        action: 'OVERRIDDEN',
        click: { verb: 'Announce' },
      } as Record<string, unknown>)
      const frame = JSON.parse(sock.sent.at(-1)!)
      expect(frame.action).toBe('post') // 'action' in payload must NOT win
      expect(frame.evil).toBeUndefined()
    })

    it('should not forward __proto__ key in outbound frame', () => {
      service.connect('ws://test/ws')
      const sock = FakeSocket.instances.at(-1)!
      sock.open()
      service.send('test', { __proto__: 'injected', data: 'ok' } as Record<string, unknown>)
      const frame = JSON.parse(sock.sent.at(-1)!)
      expect(Object.prototype.hasOwnProperty.call(frame, '__proto__')).toBeFalse()
      expect(frame.data).toBe('ok')
    })

    it('should not forward constructor or prototype keys in outbound frame', () => {
      service.connect('ws://test/ws')
      const sock = FakeSocket.instances.at(-1)!
      sock.open()
      service.send('test', {
        constructor: 'bad',
        prototype: 'also-bad',
        safe: 'yes',
      })
      const frame = JSON.parse(sock.sent.at(-1)!)
      expect(Object.prototype.hasOwnProperty.call(frame, 'constructor')).toBeFalse()
      expect(Object.prototype.hasOwnProperty.call(frame, 'prototype')).toBeFalse()
      expect(frame.safe).toBe('yes')
    })
  })
})
