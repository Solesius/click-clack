// ──────────────────────────────────────────────────────────────
// click-clack-ui / services / ws.service.ts
// WebSocket transport — reconnecting, typed message stream
// WS transport: reconnecting socket feeding a typed message Subject.
// ──────────────────────────────────────────────────────────────

import { Injectable, signal, computed } from '@angular/core';
import { Subject } from 'rxjs';
import type { WsMessage, Click } from '../models/types';

@Injectable({ providedIn: 'root' })
export class WsService {
  private ws: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  readonly connected = signal(false);
  readonly message$ = new Subject<WsMessage>();
  readonly open$ = new Subject<void>();

  readonly statusText = computed(() => this.connected() ? 'Connected' : 'Disconnected');

  connect(url = `ws://${location.host}/ws/v1/hub`): void {
    this.disconnect();
    this.ws = new WebSocket(url);

    this.ws.onopen = () => {
      this.connected.set(true);
      this.open$.next();
    };

    this.ws.onclose = () => {
      this.connected.set(false);
      this.scheduleReconnect(url);
    };

    this.ws.onerror = () => {
      this.ws?.close();
    };

    this.ws.onmessage = (ev: MessageEvent) => {
      try {
        const msg: WsMessage = JSON.parse(ev.data);
        this.message$.next(msg);
      } catch (err) {
        console.warn('[click-clack] dropping unparseable WS frame', err, ev.data);
      }
    };
  }

  disconnect(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.ws?.close();
    this.ws = null;
    this.connected.set(false);
  }

  send(action: string, payload: Record<string, unknown> = {}): void {
    if (this.ws?.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify({ action, ...payload }));
  }

  postClick(click: Click): void {
    this.send('post', { click });
  }

  subscribe(view: string): void {
    this.send('subscribe', { view });
  }

  unsubscribe(view: string): void {
    this.send('unsubscribe', { view });
  }

  private scheduleReconnect(url: string): void {
    this.reconnectTimer = setTimeout(() => this.connect(url), 2000);
  }
}
