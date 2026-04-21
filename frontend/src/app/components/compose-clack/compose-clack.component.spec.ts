import { ComponentFixture, TestBed } from '@angular/core/testing'
import { ComposeClackComponent } from './compose-clack.component'
import { HubStateService } from '../../services/hub-state.service'
import { mockHubState, type MockHubState } from '../../../testing/mocks'

describe('ComposeClackComponent', () => {
  let fixture: ComponentFixture<ComposeClackComponent>
  let cmp: ComposeClackComponent
  let state: MockHubState

  beforeEach(async () => {
    state = mockHubState()
    await TestBed.configureTestingModule({
      imports: [ComposeClackComponent],
      providers: [{ provide: HubStateService, useValue: state }],
    }).compileComponents()
    fixture = TestBed.createComponent(ComposeClackComponent)
    cmp = fixture.componentInstance
    fixture.detectChanges()
  })

  it('should render with ANNOUNCE as the default verb', () => {
    expect(cmp.verb).toBe('ANNOUNCE')
    const title = fixture.nativeElement.querySelector('h3')
    expect(title?.textContent).toContain('Post to ledger')
  })

  it('should compose flag bitmask from the four checkboxes', async () => {
    cmp.fUrgent = true
    cmp.fHitl = true
    state.postClack.and.resolveTo({ epoch: 1, task_id: 't-1' })
    await cmp.submit()
    const call = state.postClack.calls.mostRecent().args[0] as Record<string, unknown>
    // urgent=1 | hitl=4 = 5
    expect(call['flags']).toBe(5)
  })

  it('should flag invalid JSON payload but still attempt submit', async () => {
    cmp.payload = '{not json'
    state.postClack.and.resolveTo({ epoch: 2, task_id: 't-2' })
    await cmp.submit()
    expect(cmp.payloadError()).toContain('Invalid JSON')
    expect(state.postClack).toHaveBeenCalled()
  })

  it('should set result signal and clear text fields on success', async () => {
    cmp.subject = 'hello'
    cmp.payload = '{"k":1}'
    state.postClack.and.resolveTo({ epoch: 138, task_id: 't-abc' })
    await cmp.submit()
    expect(cmp.result()).toEqual({ epoch: 138, task_id: 't-abc' })
    expect(cmp.error()).toBeNull()
    expect(cmp.subject).toBe('')
    expect(cmp.payload).toBe('')
  })

  it('should set error signal when postClack throws', async () => {
    state.postClack.and.rejectWith(new Error('network kaboom'))
    await cmp.submit()
    expect(cmp.error()).toBe('network kaboom')
    expect(cmp.result()).toBeNull()
  })

  it('should surface server-side error response without setting result', async () => {
    state.postClack.and.resolveTo({ error: 'bad verb' } as unknown)
    await cmp.submit()
    expect(cmp.error()).toBe('bad verb')
    expect(cmp.result()).toBeNull()
  })

  it('should emit close on ESC', () => {
    let emitted = false
    cmp.close.subscribe(() => (emitted = true))
    cmp.onEsc()
    expect(emitted).toBeTrue()
  })

  it('should toggle submitting state around the call', async () => {
    let resolveFn!: (v: { epoch: number; task_id: string }) => void
    state.postClack.and.returnValue(
      new Promise((resolve) => {
        resolveFn = resolve
      }),
    )
    const p = cmp.submit()
    expect(cmp.submitting()).toBeTrue()
    resolveFn({ epoch: 1, task_id: 't' })
    await p
    expect(cmp.submitting()).toBeFalse()
  })
})
