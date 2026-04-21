// ──────────────────────────────────────────────────────────────
// TimelineViewComponent — real-time clack stream
// Click a row to inspect full metadata; pin via peer vote or override.
// ──────────────────────────────────────────────────────────────

import { Component, inject, ChangeDetectionStrategy, computed, signal } from '@angular/core';
import { DatePipe } from '@angular/common';
import { HubStateService } from '../../services/hub-state.service';
import { WsService } from '../../services/ws.service';
import type { Clack, PinEntry } from '../../models/types';
import { DetailModalComponent, type DetailField } from '../detail-modal/detail-modal.component';
import { ComposeClackComponent } from '../compose-clack/compose-clack.component';

const QUICK_VERBS = ['ANNOUNCE', 'CLAIM', 'PROGRESS', 'COMPLETE', 'ERROR', 'ASK', 'ANSWER'] as const;

@Component({
  selector: 'cc-timeline',
  standalone: true,
  imports: [DatePipe, DetailModalComponent, ComposeClackComponent],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-page-header">
      <div>
        <h2>Timeline</h2>
        <span class="sub">{{ rows().length }} of {{ total() }} clacks · last epoch {{ state.lastEpoch() }}</span>
      </div>
      <div class="cc-page-actions">
        <button class="cc-btn primary" (click)="composing.set(true)">＋ Post clack</button>
      </div>
    </div>

    @if (pinnedRows().length) {
      <section class="cc-pinned">
        <header>
          <span class="pin-icon">★</span>
          <span>Pinned · {{ pinnedRows().length }}</span>
        </header>
        <div class="cc-pinned-list">
          @for (row of pinnedRows(); track row.epoch) {
            <article class="cc-pin-row" (click)="open(row)">
              <span class="mono epoch">#{{ row.epoch }}</span>
              <span class="cc-verb" [attr.data-verb]="(row.verb || '').toLowerCase()">{{ row.verb }}</span>
              <span class="mono agent">{{ row.agent_id }}</span>
              <span class="subject">{{ row.subject || '—' }}</span>
              <span class="pin-meta">
                @if (pinFor(row.epoch)?.manual_override === true) { <span class="tag ovr">override</span> }
                @else { <span class="tag">{{ pinFor(row.epoch)?.votes || 0 }}/{{ pinFor(row.epoch)?.threshold }} votes</span> }
              </span>
            </article>
          }
        </div>
      </section>
    }

    <div class="cc-filters">
      @for (v of quickVerbs; track v) {
        <button class="chip" [class.active]="selectedVerbs().has(v)" (click)="toggleVerb(v)">{{ v }}</button>
      }
      @if (selectedVerbs().size) {
        <button class="chip" (click)="clearVerbs()">clear</button>
      }
      <input class="search" type="search"
             placeholder="search subject, agent, task_id, verb…"
             [value]="query()"
             (input)="query.set($any($event.target).value)">
      @if (ws.connected()) {
        <span class="live"><span class="dot"></span> live</span>
      }
    </div>

    <div class="cc-table-wrap">
      @if (!rows().length) {
        <div class="cc-empty">No clacks match the current filter.</div>
      } @else {
        <table class="cc-table">
          <thead>
            <tr>
              <th style="width: 32px"></th>
              <th style="width: 64px">Epoch</th>
              <th style="width: 110px">Verb</th>
              <th style="width: 150px">Agent</th>
              <th>Task</th>
              <th>Subject</th>
              <th style="width: 110px; text-align: right">Time</th>
            </tr>
          </thead>
          <tbody>
            @for (row of rows(); track row.epoch) {
              <tr (click)="open(row)" [class.pinned]="isPinned(row.epoch)">
                <td class="pin-cell">
                  @if (isPinned(row.epoch)) { <span class="pin-icon" title="pinned">★</span> }
                </td>
                <td class="mono">{{ row.epoch }}</td>
                <td><span class="cc-verb" [attr.data-verb]="(row.verb || '').toLowerCase()">{{ row.verb }}</span></td>
                <td class="mono">{{ row.agent_id }}</td>
                <td class="hash" [attr.title]="row.task_id">{{ row.task_id || '—' }}</td>
                <td>{{ row.subject || '' }}</td>
                <td class="time">{{ row.timestamp_us / 1000 | date:'HH:mm:ss.SSS' }}</td>
              </tr>
            }
          </tbody>
        </table>
      }
    </div>

    @if (selected(); as c) {
      <cc-detail-modal
        kind="clack"
        [title]="'epoch ' + c.epoch + ' · ' + c.verb"
        [fields]="fieldsFor(c)"
        [timestampUs]="c.timestamp_us"
        [payload]="c.payload"
        (close)="selected.set(null)"
      >
        <footer modalFooter class="cc-modal-foot cc-pin-foot">
          <div class="pin-status">
            @if (pinFor(c.epoch); as p) {
              @if (p.manual_override === true) {
                <span class="tag ovr">★ pinned by override</span>
                <small>{{ p.override_by || '—' }}</small>
              } @else if (p.manual_override === false) {
                <span class="tag warn">unpinned by override</span>
                <small>{{ p.override_by || '—' }}</small>
              } @else if (p.pinned) {
                <span class="tag">★ pinned by {{ p.votes }}/{{ p.threshold }} votes</span>
              } @else {
                <span class="tag muted">{{ p.votes }}/{{ p.threshold }} votes</span>
              }
              @if (p.voters?.length) {
                <small class="voters">{{ p.voters.join(', ') }}</small>
              }
            } @else {
              <span class="tag muted">unpinned · 0/2 votes</span>
            }
          </div>

          <div class="pin-actions">
            @if (hasVoted(c.epoch)) {
              <button class="cc-btn ghost" (click)="unvote(c.epoch)">Remove vote</button>
            } @else {
              <button class="cc-btn" (click)="vote(c.epoch)">Vote to pin</button>
            }
            <button class="cc-btn primary" (click)="forcePin(c.epoch)">Force pin</button>
            <button class="cc-btn" (click)="forceUnpin(c.epoch)">Force unpin</button>
            @if (pinFor(c.epoch)?.manual_override !== null && pinFor(c.epoch)?.manual_override !== undefined) {
              <button class="cc-btn ghost" (click)="clearOverride(c.epoch)">Clear override</button>
            }
          </div>
        </footer>
      </cc-detail-modal>
    }

    @if (composing()) {
      <cc-compose-clack (close)="composing.set(false)" />
    }
  `,
})
export class TimelineViewComponent {
  readonly state = inject(HubStateService);
  readonly ws = inject(WsService);

  readonly quickVerbs = QUICK_VERBS;
  readonly selectedVerbs = signal<Set<string>>(new Set());
  readonly query = signal('');
  readonly selected = signal<Clack | null>(null);
  readonly composing = signal(false);

  readonly total = computed(() => this.state.timeline().length);

  readonly rows = computed<Clack[]>(() => {
    const verbs = this.selectedVerbs();
    const q = this.query().trim().toLowerCase();
    const all = this.state.timeline();
    const reversed = [...all].reverse();
    if (!verbs.size && !q) return reversed;
    return reversed.filter((c) => {
      if (verbs.size && !verbs.has(c.verb?.toUpperCase?.() ?? '')) return false;
      if (!q) return true;
      return (
        c.subject?.toLowerCase().includes(q) ||
        c.agent_id?.toLowerCase().includes(q) ||
        c.task_id?.toLowerCase().includes(q) ||
        c.verb?.toLowerCase().includes(q)
      );
    });
  });

  readonly pinnedRows = computed<Clack[]>(() => {
    const pins = this.state.pinByEpoch();
    const tl = this.state.timeline();
    const result: Clack[] = [];
    for (const c of tl) {
      const p = pins.get(c.epoch);
      if (p?.pinned) result.push(c);
    }
    return result.sort((a, b) => b.epoch - a.epoch);
  });

  toggleVerb(v: string): void {
    this.selectedVerbs.update((set) => {
      const next = new Set(set);
      next.has(v) ? next.delete(v) : next.add(v);
      return next;
    });
  }

  clearVerbs(): void { this.selectedVerbs.set(new Set()); }
  open(row: Clack): void { this.selected.set(row); }

  isPinned(epoch: number): boolean { return this.state.pinnedEpochs().has(epoch); }
  pinFor(epoch: number): PinEntry | undefined { return this.state.pinByEpoch().get(epoch); }
  hasVoted(epoch: number): boolean {
    const p = this.pinFor(epoch);
    return !!p?.voters?.includes(this.state.operatorId());
  }

  vote(epoch: number):         void { void this.state.votePin(epoch, false); }
  unvote(epoch: number):       void { void this.state.votePin(epoch, true); }
  forcePin(epoch: number):     void { void this.state.pinOverride(epoch, true); }
  forceUnpin(epoch: number):   void { void this.state.pinOverride(epoch, false); }
  clearOverride(epoch: number):void { void this.state.pinOverride(epoch, null); }

  fieldsFor(c: Clack): DetailField[] {
    const flags: string[] = [];
    if (c.flags?.urgent)    flags.push('urgent');
    if (c.flags?.blocking)  flags.push('blocking');
    if (c.flags?.hitl_req)  flags.push('hitl_req');
    if (c.flags?.ephemeral) flags.push('ephemeral');
    return [
      { label: 'epoch',        value: c.epoch,        mono: true },
      { label: 'verb',         value: c.verb },
      { label: 'agent_id',     value: c.agent_id,     mono: true },
      { label: 'task_id',      value: c.task_id || '—', mono: true },
      { label: 'parent_epoch', value: c.parent_epoch, mono: true },
      { label: 'subject',      value: c.subject || '—' },
      { label: 'flags',        value: flags.length ? flags.join(', ') : '—' },
    ];
  }
}
// ──────────────────────────────────────────────────────────────
// TimelineViewComponent — real-time clack stream
// Click a row to inspect full metadata.
// ──────────────────────────────────────────────────────────────

