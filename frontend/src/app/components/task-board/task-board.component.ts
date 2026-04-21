// ──────────────────────────────────────────────────────────────
// TaskBoardViewComponent — Kanban-style task board
// Click a card to inspect full task metadata.
// ──────────────────────────────────────────────────────────────

import { Component, inject, ChangeDetectionStrategy, computed, signal } from '@angular/core';
import { HubStateService } from '../../services/hub-state.service';
import type { TaskState, TaskStatus } from '../../models/types';
import { DetailModalComponent, type DetailField } from '../detail-modal/detail-modal.component';

interface Column {
  label: string;
  statuses: TaskStatus[];
}

@Component({
  selector: 'cc-task-board',
  standalone: true,
  imports: [DetailModalComponent],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-page-header">
      <div>
        <h2>Task Board</h2>
        <span class="sub">{{ state.tasks().length }} total · {{ state.activeTasks().length }} active</span>
      </div>
    </div>

    <div class="cc-board">
      @for (col of columns; track col.label) {
        <section class="cc-board-col">
          <header>
            <h3>{{ col.label }}</h3>
            <span class="count">{{ tasksFor(col.statuses)().length }}</span>
          </header>
          <div class="col-body">
            @for (task of tasksFor(col.statuses)(); track task.task_id) {
              <article class="cc-task-card"
                       [attr.data-status]="task.status.toLowerCase()"
                       (click)="open(task)">
                <div class="tid">{{ task.task_id }}</div>
                <div class="owner">{{ task.owner_agent || 'unassigned' }}</div>
                <div class="foot">
                  <span class="cc-status" [attr.data-status]="task.status.toLowerCase()">{{ task.status }}</span>
                  @if (task.pct > 0) {
                    <span class="pct-label">{{ task.pct }}%</span>
                  }
                </div>
                @if (task.pct > 0) {
                  <div class="pct"><div class="fill" [style.width.%]="task.pct"></div></div>
                }
              </article>
            } @empty {
              <div class="empty">— empty —</div>
            }
          </div>
        </section>
      }
    </div>

    @if (selected(); as t) {
      <cc-detail-modal
        kind="task"
        [title]="t.task_id"
        [fields]="fieldsFor(t)"
        [timestampUs]="t.updated_us"
        (close)="selected.set(null)"
      />
    }
  `,
})
export class TaskBoardViewComponent {
  readonly state = inject(HubStateService);
  readonly selected = signal<TaskState | null>(null);

  readonly columns: Column[] = [
    { label: 'Unclaimed',   statuses: ['unclaimed'] },
    { label: 'In Progress', statuses: ['claimed', 'in_progress'] },
    { label: 'Done',        statuses: ['completed'] },
    { label: 'Blocked',     statuses: ['errored', 'halted'] },
  ];

  tasksFor(statuses: TaskStatus[]) {
    return computed(() => this.state.tasks().filter((t) => statuses.includes(t.status)));
  }

  open(task: TaskState): void {
    this.selected.set(task);
  }

  fieldsFor(t: TaskState): DetailField[] {
    return [
      { label: 'task_id',        value: t.task_id,        mono: true },
      { label: 'status',         value: t.status },
      { label: 'owner_agent',    value: t.owner_agent || 'unassigned', mono: true },
      { label: 'progress',       value: t.pct + '%' },
      { label: 'last_verb',      value: t.last_verb },
      { label: 'last_epoch',     value: t.last_epoch,     mono: true },
      { label: 'created_epoch',  value: t.created_epoch,  mono: true },
      { label: 'artifact_count', value: t.artifact_count, mono: true },
      { label: 'summary',        value: t.summary || '—', full: true },
    ];
  }
}
