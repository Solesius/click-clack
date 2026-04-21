import { ComponentFixture, TestBed } from '@angular/core/testing'
import { provideRouter } from '@angular/router'
import { TimelineViewComponent } from './timeline.component'
import { HubStateService } from '../../services/hub-state.service'
import { WsService } from '../../services/ws.service'
import {
  makeClack,
  makePin,
  mockHubState,
  mockWsService,
  type MockHubState,
  type MockWsService,
} from '../../../testing/mocks'

describe('TimelineViewComponent', () => {
  let fixture: ComponentFixture<TimelineViewComponent>
  let cmp: TimelineViewComponent
  let state: MockHubState
  let ws: MockWsService

  beforeEach(async () => {
    state = mockHubState()
    ws = mockWsService()
    await TestBed.configureTestingModule({
      imports: [TimelineViewComponent],
      providers: [
        provideRouter([]),
        { provide: HubStateService, useValue: state },
        { provide: WsService, useValue: ws },
      ],
    }).compileComponents()
    fixture = TestBed.createComponent(TimelineViewComponent)
    cmp = fixture.componentInstance
    fixture.detectChanges()
  })

  it('should show "No clacks match…" when timeline is empty', () => {
    const host: HTMLElement = fixture.nativeElement
    expect(host.querySelector('.cc-empty')?.textContent).toContain('No clacks')
  })

  it('should reverse timeline so the newest epoch is first', () => {
    state.timeline.set([
      makeClack({ epoch: 1 }),
      makeClack({ epoch: 2 }),
      makeClack({ epoch: 3 }),
    ])
    fixture.detectChanges()
    const epochs = cmp.rows().map((r) => r.epoch)
    expect(epochs).toEqual([3, 2, 1])
  })

  it('should surface pinned rows in descending epoch order', () => {
    state.timeline.set([makeClack({ epoch: 1 }), makeClack({ epoch: 2 })])
    state.pins.set([
      makePin({ epoch: 1, pinned: true }),
      makePin({ epoch: 2, pinned: true }),
    ])
    fixture.detectChanges()
    expect(cmp.pinnedRows().map((r) => r.epoch)).toEqual([2, 1])
  })

  it('isPinned should return true only for epochs with pinned=true', () => {
    state.pins.set([makePin({ epoch: 5, pinned: true }), makePin({ epoch: 6, pinned: false })])
    expect(cmp.isPinned(5)).toBeTrue()
    expect(cmp.isPinned(6)).toBeFalse()
    expect(cmp.isPinned(7)).toBeFalse()
  })

  it('hasVoted should be true when current operator is in voters list', () => {
    state.operatorId.set('khalil')
    state.pins.set([makePin({ epoch: 10, voters: ['khalil', 'other'] })])
    expect(cmp.hasVoted(10)).toBeTrue()
    expect(cmp.hasVoted(11)).toBeFalse()
  })

  it('should delegate vote/unvote/forcePin/forceUnpin/clearOverride to HubState', () => {
    cmp.vote(1)
    cmp.unvote(1)
    cmp.forcePin(2)
    cmp.forceUnpin(2)
    cmp.clearOverride(2)
    expect(state.votePin).toHaveBeenCalledWith(1, false)
    expect(state.votePin).toHaveBeenCalledWith(1, true)
    expect(state.pinOverride).toHaveBeenCalledWith(2, true)
    expect(state.pinOverride).toHaveBeenCalledWith(2, false)
    expect(state.pinOverride).toHaveBeenCalledWith(2, null)
  })

  it('should filter timeline rows by selected verbs', () => {
    state.timeline.set([
      makeClack({ epoch: 1, verb: 'Announce' }),
      makeClack({ epoch: 2, verb: 'Claim' }),
      makeClack({ epoch: 3, verb: 'Announce' }),
    ])
    cmp.toggleVerb('ANNOUNCE')
    fixture.detectChanges()
    expect(cmp.rows().map((r) => r.epoch)).toEqual([3, 1])
  })

  it('should filter timeline rows by text query across subject/agent/task', () => {
    state.timeline.set([
      makeClack({ epoch: 1, subject: 'deploy prod' }),
      makeClack({ epoch: 2, subject: 'refactor', agent_id: 'alpha' }),
    ])
    cmp.query.set('prod')
    fixture.detectChanges()
    expect(cmp.rows().map((r) => r.epoch)).toEqual([1])
  })

  it('should open the detail modal when open() is called with a row', () => {
    const c = makeClack({ epoch: 9 })
    cmp.open(c)
    expect(cmp.selected()).toBe(c)
  })

  it('should toggle compose modal signal when button is clicked', () => {
    expect(cmp.composing()).toBeFalse()
    cmp.composing.set(true)
    expect(cmp.composing()).toBeTrue()
  })
})
