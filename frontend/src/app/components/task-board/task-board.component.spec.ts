import { TestBed } from '@angular/core/testing'
import { provideRouter } from '@angular/router'
import { provideZonelessChangeDetection } from '@angular/core'
import { TaskBoardViewComponent } from './task-board.component'
import { HubStateService } from '../../services/hub-state.service'
import { WsService } from '../../services/ws.service'
import { mockHubState, mockWsService } from '../../../testing/mocks'
import type { TaskState } from '../../models/types'

const makeTask = (overrides: Partial<TaskState> = {}): TaskState => ({
  task_id: 't-1',
  status: 'unclaimed',
  last_verb: 'ANNOUNCE',
  last_epoch: 1,
  pct: 0,
  summary: '',
  artifact_count: 0,
  created_epoch: 1,
  updated_us: 0,
  ...overrides,
} as TaskState)

describe('TaskBoardViewComponent', () => {
  let hub: ReturnType<typeof mockHubState>

  beforeEach(() => {
    hub = mockHubState()
    TestBed.configureTestingModule({
      imports: [TaskBoardViewComponent],
      providers: [
        provideZonelessChangeDetection(),
        provideRouter([]),
        { provide: HubStateService, useValue: hub },
        { provide: WsService, useValue: mockWsService() },
      ],
    })
  })

  it('buckets tasks into the correct columns', () => {
    hub.tasks.set([
      makeTask({ task_id: 't-u', status: 'unclaimed' }),
      makeTask({ task_id: 't-c', status: 'claimed' }),
      makeTask({ task_id: 't-p', status: 'in_progress' }),
      makeTask({ task_id: 't-d', status: 'completed' }),
      makeTask({ task_id: 't-e', status: 'errored' }),
    ])
    const fx = TestBed.createComponent(TaskBoardViewComponent)
    fx.detectChanges()
    const cmp = fx.componentInstance

    expect(cmp.tasksFor(['unclaimed'])().map(t => t.task_id)).toEqual(['t-u'])
    expect(cmp.tasksFor(['claimed', 'in_progress'])().map(t => t.task_id).sort())
      .toEqual(['t-c', 't-p'])
    expect(cmp.tasksFor(['completed'])().map(t => t.task_id)).toEqual(['t-d'])
    expect(cmp.tasksFor(['errored', 'halted'])().map(t => t.task_id)).toEqual(['t-e'])
  })

  it('open() sets the selected task', () => {
    const fx = TestBed.createComponent(TaskBoardViewComponent)
    const cmp = fx.componentInstance
    const t = makeTask({ task_id: 't-x' })
    cmp.open(t)
    expect(cmp.selected()).toEqual(t)
  })

  it('fieldsFor() produces a labelled detail field list', () => {
    const fx = TestBed.createComponent(TaskBoardViewComponent)
    const cmp = fx.componentInstance
    const fields = cmp.fieldsFor(makeTask({
      task_id: 't-f', owner_agent: 'alice', pct: 55, summary: 'halfway',
    }))
    const byLabel = Object.fromEntries(fields.map(f => [f.label, f.value]))
    expect(byLabel['task_id']).toBe('t-f')
    expect(byLabel['owner_agent']).toBe('alice')
    expect(byLabel['progress']).toBe('55%')
    expect(byLabel['summary']).toBe('halfway')
  })

  it('shows empty placeholder when no tasks', () => {
    const fx = TestBed.createComponent(TaskBoardViewComponent)
    fx.detectChanges()
    const html = (fx.nativeElement as HTMLElement).textContent || ''
    expect(html).toContain('— empty —')
  })
})
