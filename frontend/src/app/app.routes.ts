// ──────────────────────────────────────────────────────────────
// App routes + bootstrap
// Top-level router wiring for the click-clack operator UI.
// ──────────────────────────────────────────────────────────────

import { type Routes } from '@angular/router';

export const routes: Routes = [
  { path: '', redirectTo: 'timeline', pathMatch: 'full' },
  {
    path: 'timeline',
    loadComponent: () => import('./components/timeline/timeline.component')
      .then(m => m.TimelineViewComponent),
  },
  {
    path: 'tasks',
    loadComponent: () => import('./components/task-board/task-board.component')
      .then(m => m.TaskBoardViewComponent),
  },
  {
    path: 'agents',
    loadComponent: () => import('./components/agent-panel/agent-panel.component')
      .then(m => m.AgentPanelViewComponent),
  },
  {
    path: 'hitl',
    loadComponent: () => import('./components/hitl-queue/hitl-queue.component')
      .then(m => m.HITLQueueViewComponent),
  },
];
