import { TestBed } from '@angular/core/testing'
import { WsService } from './ws.service'

describe('WsService', () => {
  let service: WsService

  beforeEach(() => {
    TestBed.configureTestingModule({ providers: [WsService] })
    service = TestBed.inject(WsService)
  })

  it('should expose connected=false and "Disconnected" status initially', () => {
    expect(service.connected()).toBeFalse()
    expect(service.statusText()).toBe('Disconnected')
  })

  it('should no-op send() when the socket is not open', () => {
    // If this throws, the guard is broken.
    expect(() => service.send('post', { foo: 1 })).not.toThrow()
  })

  it('should expose the typed message$ subject', () => {
    expect(service.message$).toBeDefined()
    expect(service.message$.closed).toBeFalse()
  })
})
