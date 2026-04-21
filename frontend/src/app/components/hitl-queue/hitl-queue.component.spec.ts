import { TestBed } from '@angular/core/testing'
import { provideZonelessChangeDetection } from '@angular/core'
import { HITLQueueViewComponent } from './hitl-queue.component'
import { HubStateService } from '../../services/hub-state.service'
import { WsService } from '../../services/ws.service'
import { mockHubState, mockWsService, makeClack } from '../../../testing/mocks'
import type { HITLItem } from '../../models/types'

describe('HITLQueueViewComponent', () => {
  let hub: ReturnType<typeof mockHubState>
  let ws: ReturnType<typeof mockWsService>

  const makeItem = (overrides: Partial<HITLItem> = {}): HITLItem => ({
    epoch: 42,
    resolved: false,
    clack: makeClack({ epoch: 42, verb: 'Approve', task_id: 't-42', agent_id: 'worker' }),
    ...overrides,
  })

  beforeEach(() => {
    hub = mockHubState()
    ws = mockWsService()
    TestBed.configureTestingModule({
      providers: [
        provideZonelessChangeDetection(),
        { provide: HubStateService, useValue: hub },
        { provide: WsService, useValue: ws },
      ],
    })
  })

  it('renders empty state when no pending items', () => {
    const fx = TestBed.createComponent(HITLQueueViewComponent)
    fx.detectChanges()
    const html = (fx.nativeElement as HTMLElement).textContent || ''
    expect(html).toContain('Inbox zero')
  })

  it('renders rows for each pending item', () => {
    hub.hitlQueue.set([makeItem(), makeItem({ epoch: 43 })])
    const fx = TestBed.createComponent(HITLQueueViewComponent)
    fx.detectChanges()
    const rows = (fx.nativeElement as HTMLElement).querySelectorAll('tbody tr')
    expect(rows.length).toBe(2)
  })

  it('approve() posts an Approve click with ref_epoch', () => {
    const fx = TestBed.createComponent(HITLQueueViewComponent)
    fx.componentInstance.approve({ epoch: 99, clack: { task_id: 't-99' } })
    expect(ws.postClick).toHaveBeenCalledTimes(1)
    const arg = ws.postClick.calls.mostRecent().args[0]
    expect(arg.verb).toBe('Approve')
    expect(arg.task_id).toBe('t-99')
    expect(arg.payload).toEqual({ ref_epoch: 99 })
  })

  it('reject() posts a Reject click with ref_epoch', () => {
    const fx = TestBed.createComponent(HITLQueueViewComponent)
    fx.componentInstance.reject({ epoch: 100, clack: { task_id: 't-100' } })
    const arg = ws.postClick.calls.mostRecent().args[0]
    expect(arg.verb).toBe('Reject')
    expect(arg.payload).toEqual({ ref_epoch: 100 })
  })
})
