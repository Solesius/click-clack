// ──────────────────────────────────────────────────────────────
// HubShellComponent — root app shell
// Token-driven dark/light MD3 flat layout with Fluent-style sidenav
// ──────────────────────────────────────────────────────────────

import { Component, inject, type OnInit, ChangeDetectionStrategy, signal } from '@angular/core';
import { RouterOutlet, RouterLink, RouterLinkActive } from '@angular/router';
import { MatIconModule } from '@angular/material/icon';
import { HubStateService } from '../../services/hub-state.service';
import { WsService } from '../../services/ws.service';

type Theme = 'dark' | 'light';

@Component({
  selector: 'cc-hub-shell',
  standalone: true,
  imports: [RouterOutlet, RouterLink, RouterLinkActive, MatIconModule],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-shell">
      <aside class="cc-nav">
        <div class="nav-section-label">Workspace</div>
        <a class="nav-item" routerLink="/timeline" routerLinkActive="active">
          <mat-icon>timeline</mat-icon>
          <span class="label">Timeline</span>
        </a>
        <a class="nav-item accent" routerLink="/tasks" routerLinkActive="active">
          <mat-icon>task_alt</mat-icon>
          <span class="label">Tasks</span>
          @if (state.activeTasks().length; as n) {
            <span class="count">{{ n }}</span>
          }
        </a>
        <a class="nav-item success" routerLink="/agents" routerLinkActive="active">
          <mat-icon>smart_toy</mat-icon>
          <span class="label">Agents</span>
          @if (state.onlineAgents().length; as n) {
            <span class="count">{{ n }}</span>
          }
        </a>
        <a class="nav-item warn" routerLink="/hitl" routerLinkActive="active">
          <mat-icon>pan_tool</mat-icon>
          <span class="label">HITL Queue</span>
          @if (state.pendingHitl().length; as n) {
            <span class="count">{{ n }}</span>
          }
        </a>
      </aside>

      <header class="cc-topbar">
        <div class="brand">
          <span class="brand-dot"></span>
          <span>click-clack</span>
        </div>
        <span class="spacer"></span>
        <span class="conn" [class.online]="ws.connected()">
          <span class="conn-dot"></span>
          {{ ws.statusText() }}
        </span>
        <span class="epoch">epoch {{ state.lastEpoch() }}</span>
        <button class="theme-btn" (click)="toggleTheme()" [attr.aria-label]="'switch to ' + (theme() === 'dark' ? 'light' : 'dark') + ' mode'">
          <mat-icon>{{ theme() === 'dark' ? 'light_mode' : 'dark_mode' }}</mat-icon>
        </button>
      </header>

      <main class="cc-main">
        <router-outlet />
      </main>
    </div>
  `,
})
export class HubShellComponent implements OnInit {
  readonly state = inject(HubStateService);
  readonly ws = inject(WsService);
  readonly theme = signal<Theme>(this.readTheme());

  ngOnInit(): void {
    this.state.init();
  }

  toggleTheme(): void {
    const next: Theme = this.theme() === 'dark' ? 'light' : 'dark';
    this.theme.set(next);
    document.documentElement.setAttribute('data-theme', next);
    try { localStorage.setItem('cc-theme', next); } catch { /* ignore */ }
  }

  private readTheme(): Theme {
    const attr = document.documentElement.getAttribute('data-theme');
    return attr === 'light' ? 'light' : 'dark';
  }
}
