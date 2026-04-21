// ──────────────────────────────────────────────────────────────
// AgentPanelViewComponent — agent presence + activity
// Flat card grid with pulsing online dot
// ──────────────────────────────────────────────────────────────

import { Component, inject, ChangeDetectionStrategy, computed } from '@angular/core';
import { DatePipe } from '@angular/common';
import { HubStateService } from '../../services/hub-state.service';

@Component({
  selector: 'cc-agent-panel',
  standalone: true,
  imports: [DatePipe],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-page-header">
      <div>
        <h2>Agents</h2>
        <span class="sub">{{ state.onlineAgents().length }} online · {{ state.agents().length }} known</span>
      </div>
    </div>

    @if (!sorted().length) {
      <div class="cc-empty">No agents have reported in yet.</div>
    } @else {
      <div class="cc-agents">
        @for (agent of sorted(); track agent.agent_id) {
          <div class="cc-agent" [class.online]="agent.status === 'online'">
            <span class="dot"></span>
            <div class="meta">
              <div class="id">{{ agent.agent_id }}</div>
              <div class="sub">
                {{ agent.status === 'online' ? 'online' : 'offline' }} ·
                last {{ agent.last_seen_us / 1000 | date:'HH:mm:ss' }}
              </div>
            </div>
            <span class="epoch-tag">#{{ agent.last_epoch }}</span>
          </div>
        }
      </div>
    }
  `,
})
export class AgentPanelViewComponent {
  readonly state = inject(HubStateService);
  readonly sorted = computed(() =>
    [...this.state.agents()].sort(
      (a, b) =>
        +(b.status === 'online') - +(a.status === 'online') ||
        b.last_seen_us - a.last_seen_us,
    ),
  );
}
