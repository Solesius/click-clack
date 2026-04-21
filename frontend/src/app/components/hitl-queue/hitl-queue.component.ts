// ──────────────────────────────────────────────────────────────
// HITLQueueViewComponent — human-in-the-loop approval queue
// Token-styled flat table with approve/reject icon buttons
// ──────────────────────────────────────────────────────────────

import { Component, inject, ChangeDetectionStrategy } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { HubStateService } from '../../services/hub-state.service';
import { WsService } from '../../services/ws.service';

@Component({
  selector: 'cc-hitl-queue',
  standalone: true,
  imports: [MatIconModule],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-page-header">
      <div>
        <h2>HITL Queue</h2>
        <span class="sub">{{ state.pendingHitl().length }} pending approval</span>
      </div>
    </div>

    @if (!state.pendingHitl().length) {
      <div class="cc-empty">
        <mat-icon>check_circle</mat-icon>
        <div>Inbox zero — no pending approvals.</div>
      </div>
    } @else {
      <div class="cc-table-wrap" style="border-radius: var(--cc-r-md)">
        <table class="cc-table">
          <thead>
            <tr>
              <th style="width: 64px">Epoch</th>
              <th style="width: 110px">Verb</th>
              <th style="width: 150px">Agent</th>
              <th>Subject</th>
              <th style="width: 90px; text-align: right">Actions</th>
            </tr>
          </thead>
          <tbody>
            @for (item of state.pendingHitl(); track item.epoch) {
              <tr>
                <td class="mono">{{ item.epoch }}</td>
                <td><span class="cc-verb" [attr.data-verb]="(item.clack.verb || '').toLowerCase()">{{ item.clack.verb }}</span></td>
                <td class="mono">{{ item.clack.agent_id }}</td>
                <td>{{ item.clack.subject }}</td>
                <td style="text-align: right">
                  <button class="cc-icon-btn ok" (click)="approve(item)" aria-label="approve">
                    <mat-icon>check</mat-icon>
                  </button>
                  <button class="cc-icon-btn bad" (click)="reject(item)" aria-label="reject">
                    <mat-icon>close</mat-icon>
                  </button>
                </td>
              </tr>
            }
          </tbody>
        </table>
      </div>
    }
  `,
})
export class HITLQueueViewComponent {
  readonly state = inject(HubStateService);
  private readonly ws = inject(WsService);

  approve(item: { epoch: number; clack: { task_id: string } }): void {
    this.ws.postClick({
      verb: 'Approve',
      agent_id: 'operator',
      task_id: item.clack.task_id,
      payload: { ref_epoch: item.epoch },
    });
  }

  reject(item: { epoch: number; clack: { task_id: string } }): void {
    this.ws.postClick({
      verb: 'Reject',
      agent_id: 'operator',
      task_id: item.clack.task_id,
      payload: { ref_epoch: item.epoch },
    });
  }
}
