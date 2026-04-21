// ──────────────────────────────────────────────────────────────
// DetailModalComponent — reusable record/metadata inspector
// Token-styled flat modal with JSON pretty-print + copy
// ──────────────────────────────────────────────────────────────

import {
  Component,
  ChangeDetectionStrategy,
  input,
  output,
  computed,
  HostListener,
} from '@angular/core';
import { DatePipe } from '@angular/common';

export interface DetailField {
  label: string;
  value: string | number | undefined | null;
  mono?: boolean;
  full?: boolean;
}

@Component({
  selector: 'cc-detail-modal',
  standalone: true,
  imports: [DatePipe],
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="cc-modal-backdrop" (click)="close.emit()">
      <div class="cc-modal" (click)="$event.stopPropagation()" role="dialog" aria-modal="true">
        <header class="cc-modal-head">
          <div class="cc-modal-title">
            <span class="kind">{{ kind() }}</span>
            <h3>{{ title() }}</h3>
          </div>
          <button class="cc-icon-btn" (click)="close.emit()" aria-label="close">✕</button>
        </header>

        <div class="cc-modal-body">
          @if (fields().length) {
            <dl class="cc-meta">
              @for (f of fields(); track f.label) {
                <div class="cc-meta-row" [class.full]="f.full">
                  <dt>{{ f.label }}</dt>
                  <dd [class.mono]="f.mono">{{ f.value ?? '—' }}</dd>
                </div>
              }
            </dl>
          }

          @if (timestampUs(); as ts) {
            <dl class="cc-meta">
              <div class="cc-meta-row">
                <dt>timestamp</dt>
                <dd class="mono">{{ ts / 1000 | date:'yyyy-MM-dd HH:mm:ss.SSS' }}</dd>
              </div>
            </dl>
          }

          @if (payloadJson(); as json) {
            <section class="cc-json">
              <header>
                <span>payload</span>
                <button class="cc-icon-btn ghost" (click)="copy(json)">copy</button>
              </header>
              <pre>{{ json }}</pre>
            </section>
          }
        </div>

        <ng-content select="[modalFooter]"></ng-content>
      </div>
    </div>
  `,
})
export class DetailModalComponent {
  readonly kind = input<string>('record');
  readonly title = input<string>('');
  readonly fields = input<DetailField[]>([]);
  readonly timestampUs = input<number | undefined>(undefined);
  readonly payload = input<unknown>(undefined);
  readonly close = output<void>();

  readonly payloadJson = computed<string | null>(() => {
    const p = this.payload();
    if (p === undefined || p === null || p === '') return null;
    try {
      return typeof p === 'string' ? p : JSON.stringify(p, null, 2);
    } catch {
      return String(p);
    }
  });

  @HostListener('document:keydown.escape')
  onEsc(): void {
    this.close.emit();
  }

  copy(text: string): void {
    void navigator.clipboard?.writeText(text);
  }
}
