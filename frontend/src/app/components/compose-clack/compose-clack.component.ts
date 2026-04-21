// ──────────────────────────────────────────────────────────────
// ComposeClackComponent — operator-facing "post to ledger" form
// Writes via cc.post_clack (HTTP MCP call, tagged with operator id).
// ──────────────────────────────────────────────────────────────

import {
  Component, ChangeDetectionStrategy, inject, signal, output,
  HostListener,
} from '@angular/core';
import { FormsModule } from '@angular/forms';
import { HubStateService } from '../../services/hub-state.service';

const VERBS = [
  'ANNOUNCE', 'CLAIM', 'YIELD', 'PROGRESS', 'ARTIFACT', 'COMPLETE',
  'ERROR', 'QUERY', 'RESPOND', 'OBSERVE', 'DIRECT', 'APPROVE',
  'REJECT', 'HALT', 'RESUME', 'HEARTBEAT', 'ASK', 'ANSWER', 'ACK',
] as const;

@Component({
  selector: 'cc-compose-clack',
  standalone: true,
  imports: [FormsModule],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-modal-backdrop" (click)="close.emit()">
      <div class="cc-modal cc-compose" (click)="$event.stopPropagation()" role="dialog" aria-modal="true">
        <header class="cc-modal-head">
          <div class="cc-modal-title">
            <span class="kind">compose</span>
            <h3>Post to ledger</h3>
          </div>
          <button class="cc-icon-btn ghost" (click)="close.emit()" aria-label="close">✕</button>
        </header>

        <div class="cc-modal-body cc-form">
          <label class="cc-field">
            <span>Operator</span>
            <input type="text"
                   [value]="state.operatorId()"
                   (input)="state.setOperatorId($any($event.target).value)"
                   placeholder="operator">
          </label>

          <label class="cc-field">
            <span>Verb</span>
            <select [(ngModel)]="verb" name="verb">
              @for (v of verbs; track v) { <option [value]="v">{{ v }}</option> }
            </select>
          </label>

          <label class="cc-field">
            <span>Subject</span>
            <input type="text" [(ngModel)]="subject" name="subject" placeholder="short human-readable line">
          </label>

          <div class="cc-field-row">
            <label class="cc-field">
              <span>Task ID</span>
              <input type="text" [(ngModel)]="taskId" name="task_id" placeholder="auto if empty">
            </label>
            <label class="cc-field">
              <span>Parent epoch</span>
              <input type="number" min="0" [(ngModel)]="parent" name="parent">
            </label>
          </div>

          <div class="cc-field">
            <span>Flags</span>
            <div class="cc-flags">
              <label><input type="checkbox" [(ngModel)]="fUrgent"    name="urgent"><span>urgent</span></label>
              <label><input type="checkbox" [(ngModel)]="fBlocking"  name="blocking"><span>blocking</span></label>
              <label><input type="checkbox" [(ngModel)]="fHitl"      name="hitl_req"><span>hitl_req</span></label>
              <label><input type="checkbox" [(ngModel)]="fEphemeral" name="ephemeral"><span>ephemeral</span></label>
            </div>
          </div>

          <label class="cc-field">
            <span>Payload (JSON)</span>
            <textarea rows="6"
                      [(ngModel)]="payload"
                      name="payload"
                      placeholder='{"key":"value"}'
                      spellcheck="false"></textarea>
            @if (payloadError()) { <small class="err">{{ payloadError() }}</small> }
          </label>

          @if (error(); as e) {
            <div class="cc-form-error">{{ e }}</div>
          }
          @if (result(); as r) {
            <div class="cc-form-ok">Posted at epoch <b>{{ r.epoch }}</b> · task_id <span class="mono">{{ r.task_id }}</span></div>
          }
        </div>

        <footer class="cc-modal-foot">
          <button class="cc-btn ghost" (click)="close.emit()">Close</button>
          <button class="cc-btn primary" (click)="submit()" [disabled]="submitting()">
            {{ submitting() ? 'Posting…' : 'Post clack' }}
          </button>
        </footer>
      </div>
    </div>
  `,
})
export class ComposeClackComponent {
  readonly state = inject(HubStateService);
  readonly close = output<void>();

  readonly verbs = VERBS;

  verb = 'ANNOUNCE';
  subject = '';
  taskId = '';
  parent = 0;
  fUrgent = false;
  fBlocking = false;
  fHitl = false;
  fEphemeral = false;
  payload = '';

  readonly submitting = signal(false);
  readonly error = signal<string | null>(null);
  readonly result = signal<{ epoch: number; task_id: string } | null>(null);
  readonly payloadError = signal<string | null>(null);

  @HostListener('document:keydown.escape')
  onEsc(): void { this.close.emit(); }

  private computeFlags(): number {
    return (this.fUrgent ? 1 : 0)
         | (this.fBlocking ? 2 : 0)
         | (this.fHitl ? 4 : 0)
         | (this.fEphemeral ? 8 : 0);
  }

  async submit(): Promise<void> {
    this.error.set(null);
    this.result.set(null);
    this.payloadError.set(null);

    // Validate payload eagerly; backend also falls back to {note: text}.
    const trimmed = (this.payload || '').trim();
    if (trimmed) {
      try { JSON.parse(trimmed); }
      catch (e) {
        this.payloadError.set('Invalid JSON — will be wrapped as {"note": "..."} on post.');
      }
    }

    this.submitting.set(true);
    try {
      const out = await this.state.postClack({
        verb: this.verb,
        subject: this.subject,
        task_id: this.taskId || undefined,
        parent: Number(this.parent) || 0,
        flags: this.computeFlags(),
        payload: trimmed,
      });
      const err = (out as Record<string, unknown>)['error'];
      if (typeof err === 'string') {
        this.error.set(err);
      } else {
        this.result.set({ epoch: out.epoch, task_id: out.task_id });
        // Clear text bits so the next post is blank
        this.subject = '';
        this.payload = '';
      }
    } catch (e) {
      this.error.set(e instanceof Error ? e.message : String(e));
    } finally {
      this.submitting.set(false);
    }
  }
}
