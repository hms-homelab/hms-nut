import { Component, OnDestroy, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import {
  NutApiService, DeviceStatus, UpsMetrics, DailySummary,
} from '../services/nut-api.service';
import {
  GROUP_ORDER, METRICS, METRIC_BY_KEY, MetricGroup, formatMetric, formatDuration,
} from '../shared/metrics';
import { SparklineComponent } from '../shared/sparkline.component';
import { seriesColor } from '../shared/chart-theme';

interface MetricRow { label: string; value: string; }
interface MetricSection { group: MetricGroup; rows: MetricRow[]; }

interface NodeView {
  device: DeviceStatus;
  sections: MetricSection[];
  metricCount: number;
  expanded: boolean;
  batterySpark: (number | null)[];
  loadSpark: (number | null)[];
  ageSeconds: number | null;
}

/** No MQTT for this long and the card is treated as stale rather than current. */
const STALE_AFTER_SECONDS = 600;

@Component({
  selector: 'app-dashboard',
  standalone: true,
  imports: [CommonModule, SparklineComponent],
  template: `
    <div class="head">
      <h1>Live Status</h1>
      <span class="muted">auto-refresh 5s · {{ nodes.length }} node(s)</span>
    </div>

    @if (error) { <div class="card"><span class="badge bad">error</span> {{ error }}</div> }

    <!-- Fleet roll-up across every configured node -->
    @if (nodes.length) {
      <div class="card fleet">
        <div class="tile">
          <span class="tile-label">Nodes online</span>
          <span class="tile-value">{{ fleet.online }}<span class="tile-sub">/{{ nodes.length }}</span></span>
        </div>
        <div class="tile">
          <span class="tile-label">Total draw</span>
          <span class="tile-value">{{ fleet.totalWatts == null ? '—' : (fleet.totalWatts | number:'1.0-0') }}<span class="tile-sub">W</span></span>
        </div>
        <div class="tile">
          <span class="tile-label">On battery</span>
          <span class="tile-value" [class.alarm]="fleet.onBattery > 0">{{ fleet.onBattery }}</span>
        </div>
        <div class="tile">
          <span class="tile-label">Lowest battery</span>
          <span class="tile-value" [class.alarm]="fleet.minBattery != null && fleet.minBattery < 20">{{ fleet.minBattery == null ? '—' : (fleet.minBattery | number:'1.0-0') }}<span class="tile-sub">%</span></span>
        </div>
        <div class="tile">
          <span class="tile-label">Shortest runtime</span>
          <span class="tile-value">{{ fleet.minRuntime == null ? '—' : shortRuntime(fleet.minRuntime) }}</span>
        </div>
        <div class="tile">
          <span class="tile-label">Stale</span>
          <span class="tile-value" [class.warn-text]="fleet.stale > 0">{{ fleet.stale }}</span>
        </div>
      </div>
    }

    <!-- Daily energy summary (LLM), persisted server-side -->
    @if (summary) {
      <div class="card summary">
        <div class="card-head">
          <h3>Daily Energy Summary <span class="muted">· {{ summary.date }}</span></h3>
          <button class="ghost sm" (click)="regenerate()" [disabled]="generating">
            {{ generating ? 'Generating…' : 'Regenerate' }}
          </button>
        </div>
        <p class="summary-text">{{ summary.summary }}</p>
        <div class="summary-foot muted">
          @if (summary.model) { <span>model {{ summary.model }}</span> }
          @if (older.length) {
            <button class="linky" (click)="showOlder = !showOlder">
              {{ showOlder ? 'hide' : 'show' }} {{ older.length }} earlier
            </button>
          }
        </div>
        @if (showOlder) {
          <div class="older">
            @for (s of older; track s.date) {
              <div class="older-item">
                <div class="muted mono">{{ s.date }}</div>
                <p>{{ s.summary }}</p>
              </div>
            }
          </div>
        }
        @if (summaryError) { <p class="muted">{{ summaryError }}</p> }
      </div>
    }

    <div class="grid">
      @for (n of nodes; track n.device.mqtt_device_id) {
        <div class="card" [class.stale]="isStale(n)">
          <div class="card-head">
            <h3>{{ n.device.friendly_name || n.device.mqtt_device_id }}</h3>
            @if (n.device.online) {
              <span class="badge" [class.ok]="isOnline(n.device)" [class.warn]="isOnBattery(n.device)"
                    [class.bad]="isLowBattery(n.device)">{{ statusLabel(n.device) }}</span>
            } @else {
              <span class="badge off">offline</span>
            }
          </div>
          <div class="sub muted">
            <span class="mono">{{ n.device.mqtt_device_id }}</span>
            <span>{{ lastSeen(n) }}</span>
          </div>

          @if (n.device.metrics) {
            <div class="metric">
              <div class="metric-row"><span class="muted">Battery</span>
                <span class="val">{{ fmt(n.device.metrics.battery_charge, '%') }}</span></div>
              <div class="meter"><span [style.width.%]="clamp(n.device.metrics.battery_charge)"></span></div>
              @if (n.batterySpark.length > 1) {
                <div class="spark">
                  <app-sparkline [values]="n.batterySpark" [color]="sparkColor(0)"></app-sparkline>
                  <span class="spark-note muted">24 h</span>
                </div>
              }
            </div>

            <div class="metric">
              <div class="metric-row"><span class="muted">Load</span>
                <span class="val">{{ fmt(n.device.metrics.load_percentage, '%') }}
                  @if (n.device.metrics.load_watts != null) {
                    <span class="muted">({{ n.device.metrics.load_watts | number:'1.0-0' }} W)</span>
                  }
                </span></div>
              <div class="meter"><span [style.width.%]="clamp(n.device.metrics.load_percentage)"></span></div>
              @if (n.loadSpark.length > 1) {
                <div class="spark">
                  <app-sparkline [values]="n.loadSpark" [color]="sparkColor(1)"></app-sparkline>
                  <span class="spark-note muted">24 h</span>
                </div>
              }
            </div>

            <table class="mini">
              <tr><th>Input</th><td class="val">{{ fmt(n.device.metrics.input_voltage, ' V') }}</td></tr>
              <tr><th>Runtime</th><td class="val">{{ runtime(n.device.metrics.battery_runtime) }}</td></tr>
              @if (n.device.metrics.output_voltage != null) {
                <tr><th>Output</th><td class="val">{{ fmt(n.device.metrics.output_voltage, ' V') }}</td></tr>
              }
              @if (n.device.metrics.temperature != null) {
                <tr><th>Temp</th><td class="val">{{ fmt(n.device.metrics.temperature, ' °C') }}</td></tr>
              }
            </table>

            @if (n.metricCount) {
              <button class="disclose" (click)="n.expanded = !n.expanded"
                      [attr.aria-expanded]="n.expanded">
                <span class="chev" [class.open]="n.expanded">▸</span>
                {{ n.expanded ? 'Hide' : 'Show' }} all metrics ({{ n.metricCount }})
              </button>
            }

            @if (n.expanded) {
              <div class="all-metrics">
                @for (sec of n.sections; track sec.group) {
                  <div class="section">
                    <h4>{{ sec.group }}</h4>
                    <table class="mini">
                      @for (r of sec.rows; track r.label) {
                        <tr><th>{{ r.label }}</th><td class="val">{{ r.value }}</td></tr>
                      }
                    </table>
                  </div>
                }
              </div>
            }
          } @else {
            <p class="muted">No live data yet.</p>
          }
        </div>
      }
      @if (!nodes.length && !error) { <p class="muted">Loading…</p> }
    </div>
  `,
  styles: [`
    .head { display:flex; align-items:baseline; justify-content:space-between; margin-bottom:14px; }
    h1 { margin:0; }
    .card-head { display:flex; align-items:center; justify-content:space-between; gap:10px; }
    .card-head h3 { margin:0 0 2px; }
    .sub { display:flex; justify-content:space-between; gap:10px; font-size:12px; margin-bottom:10px; }
    .mono { font-family: ui-monospace, monospace; }

    /* Expanding one card must not stretch its row-mates to match. */
    .grid { align-items:start; }
    .fleet { display:grid; grid-template-columns:repeat(auto-fit, minmax(120px,1fr)); gap:8px 18px; }
    .tile { display:flex; flex-direction:column; gap:2px; }
    .tile-label { color:var(--muted); font-size:12px; }
    .tile-value { font-size:24px; font-weight:600; line-height:1.1; }
    .tile-value.alarm { color:var(--bad); }
    .tile-value.warn-text { color:var(--warn); }
    .tile-sub { font-size:13px; font-weight:400; color:var(--muted); margin-left:3px; }

    .summary .summary-text { margin:8px 0 6px; line-height:1.55; white-space:pre-wrap; }
    .summary-foot { display:flex; gap:14px; align-items:center; font-size:12px; }
    .older { margin-top:10px; border-top:1px solid var(--border); padding-top:8px; }
    .older-item { margin-bottom:12px; }
    .older-item p { margin:3px 0 0; line-height:1.5; white-space:pre-wrap; }
    .linky { background:none; border:none; color:var(--accent); padding:0; font-size:12px; cursor:pointer; }
    button.sm { padding:5px 10px; font-size:13px; }

    .metric { margin:10px 0 14px; }
    .metric-row { display:flex; justify-content:space-between; margin-bottom:4px; }
    .spark { position:relative; margin-top:8px; }
    .spark-note { position:absolute; right:0; top:-2px; font-size:10px; letter-spacing:.04em; }
    table.mini { margin-top:4px; }
    table.mini th { width:52%; font-weight:400; }
    table.mini td { text-align:right; font-variant-numeric: tabular-nums; }

    .card.stale { opacity:.62; }

    .disclose {
      margin-top:12px; background:none; border:none; color:var(--accent);
      padding:0; font-size:13px; cursor:pointer; display:flex; align-items:center; gap:6px;
    }
    .chev { display:inline-block; transition:transform .12s ease; }
    .chev.open { transform:rotate(90deg); }

    .all-metrics { margin-top:6px; border-top:1px solid var(--border); }
    .section h4 { margin:12px 0 0; font-size:12px; text-transform:uppercase;
                  letter-spacing:.06em; color:var(--muted); font-weight:600; }
    .section table.mini { margin-top:2px; }
    .section table.mini th, .section table.mini td { padding:5px 0; border-bottom:none; }
  `],
})
export class DashboardComponent implements OnInit, OnDestroy {
  nodes: NodeView[] = [];
  error = '';

  summary: DailySummary | null = null;
  older: DailySummary[] = [];
  showOlder = false;
  summaryError = '';
  generating = false;

  fleet = {
    online: 0, totalWatts: null as number | null, onBattery: 0,
    minBattery: null as number | null, minRuntime: null as number | null, stale: 0,
  };

  private timer: any;
  private sparkTimer: any;
  private summaryTimer: any;
  /** mqtt_device_id → 24h trend, refreshed on a slower cadence than live status. */
  private sparks: Record<string, { battery: (number | null)[]; load: (number | null)[] }> = {};

  constructor(private api: NutApiService) {}

  ngOnInit() {
    this.load();
    this.loadSummaries();
    this.timer = setInterval(() => this.load(), 5000);
    this.sparkTimer = setInterval(() => this.loadSparks(), 5 * 60 * 1000);
    this.summaryTimer = setInterval(() => this.loadSummaries(), 5 * 60 * 1000);
  }

  ngOnDestroy() {
    clearInterval(this.timer);
    clearInterval(this.sparkTimer);
    clearInterval(this.summaryTimer);
  }

  load() {
    this.api.getDevices().subscribe({
      next: devices => {
        const first = this.nodes.length === 0;
        const expandedIds = new Set(this.nodes.filter(n => n.expanded).map(n => n.device.mqtt_device_id));
        this.nodes = devices.map(d => this.toView(d, expandedIds.has(d.mqtt_device_id)));
        this.computeFleet();
        this.error = '';
        if (first) this.loadSparks();
      },
      error: e => this.error = e?.message || 'failed to load devices',
    });
  }

  private loadSparks() {
    if (!this.nodes.length) return;
    const ids = this.nodes.map(n => n.device.mqtt_device_id);
    this.api.getHistory(ids, 24).subscribe({
      next: r => {
        for (const s of r.series || []) {
          this.sparks[s.device] = {
            battery: s.points.map(p => p['battery_charge'] as number | null),
            load: s.points.map(p => p['load_percentage'] as number | null),
          };
        }
        for (const n of this.nodes) {
          const sp = this.sparks[n.device.mqtt_device_id];
          n.batterySpark = sp?.battery ?? [];
          n.loadSpark = sp?.load ?? [];
        }
      },
      // A missing trend is cosmetic — leave the card's live numbers alone.
      error: () => {},
    });
  }

  private loadSummaries() {
    this.api.getSummaries(14).subscribe({
      next: r => {
        const list = r.summaries || [];
        this.summary = list.length ? list[0] : null;
        this.older = list.slice(1);
        this.summaryError = list.length ? '' :
          (r.enabled ? 'No summary generated yet.' : 'Summaries disabled (LLM_ENABLED=false).');
        // Keep the card visible with an explanation even when there is nothing yet.
        if (!this.summary) {
          this.summary = { date: '—', summary: this.summaryError, model: '', generated_at: '' };
          this.summaryError = '';
        }
      },
      error: () => { this.summary = null; },
    });
  }

  regenerate() {
    this.generating = true;
    this.summaryError = '';
    this.api.generateSummary().subscribe({
      next: () => { this.generating = false; this.loadSummaries(); },
      error: e => {
        this.generating = false;
        this.summaryError = e?.error?.message || e?.message || 'summary generation failed';
      },
    });
  }

  // ── view assembly ──

  private toView(d: DeviceStatus, expanded: boolean): NodeView {
    const sections = this.buildSections(d.metrics);
    const sp = this.sparks[d.mqtt_device_id];
    return {
      device: d,
      sections,
      metricCount: sections.reduce((n, s) => n + s.rows.length, 0),
      expanded,
      batterySpark: sp?.battery ?? [],
      loadSpark: sp?.load ?? [],
      ageSeconds: this.ageSeconds(d.metrics),
    };
  }

  /** Every reported field, grouped — including the ones the summary rows omit. */
  private buildSections(m: UpsMetrics | null): MetricSection[] {
    if (!m) return [];
    const out: MetricSection[] = [];
    for (const group of GROUP_ORDER) {
      const rows: MetricRow[] = [];
      for (const def of METRICS) {
        if (def.group !== group) continue;
        const raw = m[def.key];
        if (raw === undefined || raw === null || raw === '') continue;
        rows.push({ label: def.label, value: formatMetric(def, raw) });
      }
      if (rows.length) out.push({ group, rows });
    }
    return out;
  }

  private computeFleet() {
    let online = 0, onBattery = 0, stale = 0;
    let watts: number | null = null;
    let minBattery: number | null = null;
    let minRuntime: number | null = null;

    for (const n of this.nodes) {
      const d = n.device, m = d.metrics;
      if (d.online) online++;
      if (this.isStale(n)) stale++;
      if (this.isOnBattery(d)) onBattery++;
      if (!m) continue;
      if (typeof m.load_watts === 'number') watts = (watts ?? 0) + m.load_watts;
      if (typeof m.battery_charge === 'number') {
        minBattery = minBattery == null ? m.battery_charge : Math.min(minBattery, m.battery_charge);
      }
      if (typeof m.battery_runtime === 'number') {
        minRuntime = minRuntime == null ? m.battery_runtime : Math.min(minRuntime, m.battery_runtime);
      }
    }
    this.fleet = { online, totalWatts: watts, onBattery, minBattery, minRuntime, stale };
  }

  private ageSeconds(m: UpsMetrics | null): number | null {
    if (!m?.timestamp) return null;
    const t = Date.parse(m.timestamp);
    if (isNaN(t)) return null;
    return Math.max(0, (Date.now() - t) / 1000);
  }

  // ── template helpers ──

  sparkColor(i: number): string { return seriesColor(i); }

  isStale(n: NodeView): boolean {
    return n.ageSeconds != null && n.ageSeconds > STALE_AFTER_SECONDS;
  }

  lastSeen(n: NodeView): string {
    if (n.ageSeconds == null) return '';
    const s = n.ageSeconds;
    if (s < 90) return `last seen ${Math.round(s)}s ago`;
    return `last seen ${formatDuration(s)} ago`;
  }

  shortRuntime(sec: number): string { return formatDuration(sec); }

  clamp(v: number | null | undefined): number {
    if (v == null) return 0;
    return Math.max(0, Math.min(100, v));
  }

  fmt(v: unknown, unit: string): string {
    if (typeof v !== 'number') return '—';
    return `${Math.round(v * 10) / 10}${unit}`;
  }

  runtime(sec: unknown): string {
    return typeof sec === 'number' ? formatDuration(sec) : '—';
  }

  private status(d: DeviceStatus): string { return (d.metrics?.ups_status || '').toUpperCase(); }
  isOnline(d: DeviceStatus): boolean { const s = this.status(d); return s.includes('OL') || s.includes('ONLINE'); }
  isOnBattery(d: DeviceStatus): boolean {
    const s = this.status(d);
    return s.includes('OB') || s.includes('BATTERY') || d.metrics?.power_failure === true;
  }
  isLowBattery(d: DeviceStatus): boolean {
    const s = this.status(d);
    return s.includes('LB') || (typeof d.metrics?.battery_charge === 'number' && d.metrics.battery_charge < 20);
  }
  statusLabel(d: DeviceStatus): string {
    if (this.isLowBattery(d)) return 'low battery';
    if (this.isOnBattery(d)) return 'on battery';
    if (this.isOnline(d)) return 'online';
    return d.metrics?.ups_status || 'unknown';
  }
}
