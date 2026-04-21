import { TestBed } from '@angular/core/testing'
import { provideZonelessChangeDetection } from '@angular/core'
import { AgentPanelViewComponent } from './agent-panel.component'
import { HubStateService } from '../../services/hub-state.service'
import { mockHubState } from '../../../testing/mocks'
import type { AgentPresence } from '../../models/types'

const makeAgent = (overrides: Partial<AgentPresence> = {}): AgentPresence => ({
  agent_id: 'agent-a',
  status: 'online',
  last_epoch: 1,
  last_seen_us: 1_700_000_000_000_000,
  capabilities: [],
  load: 0,
  model: '',
  ...overrides,
} as AgentPresence)

describe('AgentPanelViewComponent', () => {
  let hub: ReturnType<typeof mockHubState>

  beforeEach(() => {
    hub = mockHubState()
    TestBed.configureTestingModule({
      imports: [AgentPanelViewComponent],
      providers: [
        provideZonelessChangeDetection(),
        { provide: HubStateService, useValue: hub },
      ],
    })
  })

  it('sorts online agents first, then by recency', () => {
    hub.agents.set([
      makeAgent({ agent_id: 'a-offline-recent', status: 'offline', last_seen_us: 200 }),
      makeAgent({ agent_id: 'a-online-old', status: 'online',  last_seen_us: 100 }),
      makeAgent({ agent_id: 'a-online-new', status: 'online',  last_seen_us: 300 }),
    ])
    const fx = TestBed.createComponent(AgentPanelViewComponent)
    const ids = fx.componentInstance.sorted().map(a => a.agent_id)
    expect(ids).toEqual(['a-online-new', 'a-online-old', 'a-offline-recent'])
  })

  it('shows empty placeholder when no agents', () => {
    const fx = TestBed.createComponent(AgentPanelViewComponent)
    fx.detectChanges()
    const html = (fx.nativeElement as HTMLElement).textContent || ''
    expect(html).toContain('No agents')
  })

  it('marks online agents with the online class', () => {
    hub.agents.set([makeAgent({ agent_id: 'hot', status: 'online' })])
    const fx = TestBed.createComponent(AgentPanelViewComponent)
    fx.detectChanges()
    const card = (fx.nativeElement as HTMLElement).querySelector('.cc-agent')
    expect(card?.classList.contains('online')).toBeTrue()
  })
})
