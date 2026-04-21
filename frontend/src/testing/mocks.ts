// Shared test utilities — smart mocks for HubStateService and WsService.
// Uses real Angular signals so tests can drive reactive state from the outside.

import { type Signal, type WritableSignal, computed, signal } from '@angular/core'
import { Subject } from 'rxjs'
import type {
  AgentPresence,
  Clack,
  HITLItem,
  PinEntry,
  TaskState,
  WsMessage,
} from '../app/models/types'

export interface MockWsService {
  connected: WritableSignal<boolean>
  statusText: Signal<string>
  message$: Subject<WsMessage>
  open$: Subject<void>
  connect: jasmine.Spy
  disconnect: jasmine.Spy
  send: jasmine.Spy
  subscribe: jasmine.Spy
  unsubscribe: jasmine.Spy
  postClick: jasmine.Spy
}

export const mockWsService = (): MockWsService => {
  const connected = signal(false)
  return {
    connected,
    statusText: computed(() => (connected() ? 'Connected' : 'Disconnected')),
    message$: new Subject<WsMessage>(),
    open$: new Subject<void>(),
    connect: jasmine.createSpy('connect'),
    disconnect: jasmine.createSpy('disconnect'),
    send: jasmine.createSpy('send'),
    subscribe: jasmine.createSpy('subscribe'),
    unsubscribe: jasmine.createSpy('unsubscribe'),
    postClick: jasmine.createSpy('postClick'),
  }
}

export interface MockHubState {
  operatorId: WritableSignal<string>
  timeline: WritableSignal<Clack[]>
  tasks: WritableSignal<TaskState[]>
  agents: WritableSignal<AgentPresence[]>
  hitlQueue: WritableSignal<HITLItem[]>
  pins: WritableSignal<PinEntry[]>
  lastEpoch: WritableSignal<number>
  activeTasks: Signal<TaskState[]>
  onlineAgents: Signal<AgentPresence[]>
  pendingHitl: Signal<HITLItem[]>
  totalEpochs: Signal<number>
  pinnedEpochs: Signal<Set<number>>
  pinByEpoch: Signal<Map<number, PinEntry>>
  init: jasmine.Spy
  setOperatorId: jasmine.Spy
  callTool: jasmine.Spy
  postClack: jasmine.Spy
  refreshPins: jasmine.Spy
  votePin: jasmine.Spy
  pinOverride: jasmine.Spy
}

export const mockHubState = (): MockHubState => {
  const operatorId = signal('operator')
  const timeline = signal<Clack[]>([])
  const tasks = signal<TaskState[]>([])
  const agents = signal<AgentPresence[]>([])
  const hitlQueue = signal<HITLItem[]>([])
  const pins = signal<PinEntry[]>([])
  const lastEpoch = signal(0)

  return {
    operatorId,
    timeline,
    tasks,
    agents,
    hitlQueue,
    pins,
    lastEpoch,
    activeTasks: computed(() =>
      tasks().filter((t) => t.status !== 'completed' && t.status !== 'errored'),
    ),
    onlineAgents: computed(() => agents().filter((a) => a.status === 'online')),
    pendingHitl: computed(() => hitlQueue().filter((h) => !h.resolved)),
    totalEpochs: computed(() => lastEpoch()),
    pinnedEpochs: computed(
      () => new Set(pins().filter((p) => p.pinned).map((p) => p.epoch)),
    ),
    pinByEpoch: computed(() => {
      const m = new Map<number, PinEntry>()
      for (const p of pins()) m.set(p.epoch, p)
      return m
    }),
    init: jasmine.createSpy('init'),
    setOperatorId: jasmine.createSpy('setOperatorId').and.callFake((v: string) => {
      operatorId.set(v || 'operator')
    }),
    callTool: jasmine
      .createSpy('callTool')
      .and.returnValue(Promise.resolve({} as unknown)),
    postClack: jasmine
      .createSpy('postClack')
      .and.returnValue(Promise.resolve({ epoch: 1, task_id: 't-1' })),
    refreshPins: jasmine.createSpy('refreshPins').and.returnValue(Promise.resolve()),
    votePin: jasmine.createSpy('votePin').and.returnValue(Promise.resolve()),
    pinOverride: jasmine.createSpy('pinOverride').and.returnValue(Promise.resolve()),
  }
}

export const makeClack = (overrides: Partial<Clack> = {}): Clack =>
  ({
    epoch: 1,
    timestamp_us: 1_700_000_000_000_000,
    verb: 'Announce',
    flags: { urgent: false, blocking: false, hitl_req: false, ephemeral: false },
    parent_epoch: 0,
    agent_id: 'agent-a',
    task_id: 't-1',
    subject: 'hello',
    payload: { note: 'hi' },
    ...overrides,
  }) as Clack

export const makePin = (overrides: Partial<PinEntry> = {}): PinEntry => ({
  epoch: 1,
  pinned: false,
  votes: 0,
  voters: [],
  threshold: 2,
  manual_override: null,
  override_by: '',
  updated_us: 0,
  ...overrides,
})
