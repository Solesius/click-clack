// ──────────────────────────────────────────────────────────────
// click-clack-ui / services / hub-state.service.ts
// Signal-based reactive store — materializes WebSocket events
// Reactive store — materializes WebSocket events into Signals.
// ──────────────────────────────────────────────────────────────

import { Injectable, signal, computed, inject, DestroyRef } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { WsService } from './ws.service';
import type {
  Clack, TaskState, AgentPresence, HITLItem, WsMessage, PinEntry
} from '../models/types';

@Injectable({ providedIn: 'root' })
export class HubStateService {
  private readonly ws = inject(WsService);
  private readonly destroyRef = inject(DestroyRef);

  // ── Operator identity (used as `as_agent` for operator-originated calls) ──
  // F-10: use sessionStorage so the declared operator identity does not
  // survive tab close. A persistent cross-session "who am I" is an auth
  // decision that belongs to the hub, not a tab-local UI hint.
  readonly operatorId = signal<string>(
    sessionStorage.getItem('cc.operator_id') || 'operator',
  );

  setOperatorId(id: string): void {
    const v = (id || 'operator').trim() || 'operator';
    this.operatorId.set(v);
    sessionStorage.setItem('cc.operator_id', v);
  }

  // ── Signals ─────────────────────────────────────────────
  readonly timeline    = signal<Clack[]>([]);
  readonly tasks       = signal<TaskState[]>([]);
  readonly agents      = signal<AgentPresence[]>([]);
  readonly hitlQueue   = signal<HITLItem[]>([]);
  readonly pins        = signal<PinEntry[]>([]);
  readonly lastEpoch   = signal(0);

  // ── Computed ────────────────────────────────────────────
  readonly activeTasks   = computed(() => this.tasks().filter(t => t.status !== 'completed' && t.status !== 'errored'));
  readonly onlineAgents  = computed(() => this.agents().filter(a => a.status === 'online'));
  readonly pendingHitl   = computed(() => this.hitlQueue().filter(h => !h.resolved));
  readonly totalEpochs   = computed(() => this.lastEpoch());
  readonly pinnedEpochs  = computed(() => new Set(this.pins().filter(p => p.pinned).map(p => p.epoch)));
  readonly pinByEpoch    = computed(() => {
    const m = new Map<number, PinEntry>();
    for (const p of this.pins()) m.set(p.epoch, p);
    return m;
  });

  constructor() {
    this.ws.message$
      .pipe(takeUntilDestroyed(this.destroyRef))
      .subscribe((msg) => this.handleMessage(msg));
  }

  init(): void {
    // Re-subscribe on every open (handles initial connect + reconnects).
    this.ws.open$
      .pipe(takeUntilDestroyed(this.destroyRef))
      .subscribe(() => {
        this.ws.subscribe('timeline');
        this.ws.subscribe('tasks');
        this.ws.subscribe('agents');
        this.ws.subscribe('hitl');
        this.refreshPins();
      });

    this.ws.connect();
  }

  // ── MCP tool call (HTTP) ─────────────────────────────────
  async callTool<T = unknown>(tool: string, args: Record<string, unknown> = {}): Promise<T> {
    const body = {
      tool,
      args: { as_agent: this.operatorId(), ...args },
      agent_id: this.operatorId(),
    };
    const resp = await fetch('/api/v1/mcp/call', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!resp.ok) throw new Error(`mcp ${tool} http ${resp.status}`);
    return (await resp.json()) as T;
  }

  // ── Ledger post ─────────────────────────────────────────
  async postClack(draft: {
    verb: string;
    task_id?: string;
    subject?: string;
    flags?: number;
    parent?: number;
    payload?: Record<string, unknown> | string;
  }): Promise<{ epoch: number; task_id: string } & Record<string, unknown>> {
    let payloadObj: Record<string, unknown> = {};
    if (typeof draft.payload === 'string') {
      const s = draft.payload.trim();
      if (s.length) {
        try { payloadObj = JSON.parse(s); }
        catch { payloadObj = { note: s }; }
      }
    } else if (draft.payload && typeof draft.payload === 'object') {
      payloadObj = draft.payload;
    }
    return this.callTool('cc.post_clack', {
      verb:    draft.verb,
      task_id: draft.task_id || undefined,
      subject: draft.subject || undefined,
      flags:   draft.flags ?? 0,
      parent:  draft.parent ?? 0,
      payload: payloadObj,
    });
  }

  // ── Pins ────────────────────────────────────────────────
  async refreshPins(): Promise<void> {
    try {
      const list = await this.callTool<PinEntry[]>('cc.query_pins');
      this.pins.set(Array.isArray(list) ? list : []);
    } catch (e) {
      console.warn('[click-clack] refreshPins failed', e);
    }
  }

  async votePin(epoch: number, unvote = false): Promise<void> {
    await this.callTool('cc.vote_pin', { epoch, unvote });
    await this.refreshPins();
  }

  async pinOverride(epoch: number, pinned: boolean | null): Promise<void> {
    await this.callTool('cc.pin_override', { epoch, pinned });
    await this.refreshPins();
  }

  private handleMessage(msg: WsMessage): void {
    switch (msg.type) {
      case 'clack': {
        const clack = msg.data as Clack;
        this.timeline.update(prev => [...prev, clack]);
        this.lastEpoch.set(clack.epoch);
        // React to pin-governance clacks so every operator sees live updates.
        if (clack.subject?.startsWith('pin:') || clack.subject?.startsWith('pin_override:')) {
          this.refreshPins();
        }
        break;
      }
      case 'snapshot':
        this.applySnapshot(msg.view!, msg.data);
        break;
      case 'ack':
        if (msg.epoch) this.lastEpoch.set(msg.epoch);
        break;
      case 'error':
        console.error('[click-clack]', msg.message);
        break;
    }
  }

  private applySnapshot(view: string, data: unknown): void {
    const arr = data as unknown[];
    switch (view) {
      case 'timeline':
        this.timeline.set(arr as Clack[]);
        if (arr.length) this.lastEpoch.set((arr[arr.length - 1] as Clack).epoch);
        break;
      case 'tasks':
        this.tasks.set(arr as TaskState[]);
        break;
      case 'agents':
        this.agents.set(arr as AgentPresence[]);
        break;
      case 'hitl':
        this.hitlQueue.set(arr as HITLItem[]);
        break;
    }
  }
}
