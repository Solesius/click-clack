// ──────────────────────────────────────────────────────────────
// click-clack-ui / models / types.ts
// Shared DTOs mirroring the backend wire format (clack_to_json etc).
// ──────────────────────────────────────────────────────────────

export type Verb =
  | 'Announce' | 'Claim' | 'Yield' | 'Progress'
  | 'Artifact' | 'Complete' | 'Error' | 'Query'
  | 'Respond' | 'Observe' | 'Direct' | 'Approve'
  | 'Reject' | 'Halt' | 'Resume' | 'Heartbeat';

export type TaskStatus =
  | 'unclaimed' | 'claimed' | 'in_progress' | 'completed' | 'errored' | 'halted';

// Wire format from backend clack_to_json — flat structure
export interface ClackFlags {
  urgent: boolean;
  blocking: boolean;
  hitl_req: boolean;
  ephemeral: boolean;
}

export interface Clack {
  epoch: number;
  timestamp_us: number;
  verb: Verb;
  flags: ClackFlags;
  parent_epoch: number;
  agent_id: string;
  task_id: string;
  subject: string;
  payload: Record<string, unknown> | string;
}

export interface Click {
  verb: string;
  agent_id: string;
  task_id?: string;
  subject?: string;
  flags?: number;
  parent?: number;
  payload: Record<string, unknown>;
}

// Wire format from backend task_state_to_json
export interface TaskState {
  task_id: string;
  status: TaskStatus;
  owner_agent?: string;
  created_epoch: number;
  last_epoch: number;
  last_verb: string;
  pct: number;
  summary: string;
  artifact_count: number;
  updated_us: number;
}

// Wire format from backend presence_to_json
export interface AgentPresence {
  agent_id: string;
  status: string;      // 'online' | 'idle' | 'offline'
  last_epoch: number;
  last_seen_us: number;
  capabilities: string[];
  model: string;
  load: number;
}

export interface HITLItem {
  epoch: number;
  clack: Clack;
  resolved: boolean;
}

export interface ArtifactEntry {
  epoch: number;
  task_id: string;
  agent_id: string;
  path: string;
  mime: string;
}

export interface WsMessage {
  type: 'clack' | 'snapshot' | 'ack' | 'error';
  view?: string;
  data?: unknown;
  epoch?: number;
  message?: string;
}

export interface PinEntry {
  epoch: number;
  pinned: boolean;
  votes: number;
  voters: string[];
  threshold: number;
  manual_override: boolean | null;
  override_by: string;
  updated_us: number;
  clack?: Clack;
}
