import {
  AfterViewInit, Component, ElementRef, OnDestroy, OnInit, QueryList, ViewChildren,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Chart } from 'chart.js/auto';
import { NutApiService, DeviceStatus, HistorySeries } from '../services/nut-api.service';
import {
  AXIS_LABELS, AXIS_ORDER, CHARTED_METRICS, DEFAULT_CHART_KEYS, METRIC_BY_KEY,
  MetricAxis, MetricDef, chartUnit, chartValue, formatMetric, seriesName,
} from '../shared/metrics';
import { lineChartOptions, seriesColor, INK } from '../shared/chart-theme';

type Mode = 'per-node' | 'per-metric';

interface ChartBlock {
  id: string;
  title: string;
  unit: string;
  yMin?: number;
  yMax?: number;
  data: any;
}

interface StatRow {
  name: string;
  color: string;
  unit: string;
  min: string;
  avg: string;
  max: string;
  last: string;
  samples: number;
}

/**
 * Draws each series' name at its final point when there are few enough series
 * to do so without collisions, so identity is never carried by color alone.
 */
const directLabels = {
  id: 'hmsDirectLabels',
  afterDatasetsDraw(chart: any) {
    const sets = chart.data.datasets || [];
    if (sets.length < 2 || sets.length > 4) return;
    const ctx = chart.ctx;
    const area = chart.chartArea;
    if (!area) return;

    // Collect every series' final plotted point first, then lay the labels out
    // as a set — labelling each one greedily drops a series whenever two lines
    // converge, which reads as a bug rather than as a deliberate omission.
    const GAP = 14;
    const marks: { label: string; color: string; x: number; y: number }[] = [];

    sets.forEach((ds: any, i: number) => {
      const pts = chart.getDatasetMeta(i)?.data || [];
      for (let j = pts.length - 1; j >= 0; j--) {
        const raw = ds.data[j];
        const y = raw && typeof raw === 'object' ? raw.y : raw;
        if (y === null || y === undefined) continue;
        const p = pts[j];
        if (!p || p.x < area.left || p.x > area.right) break;
        marks.push({ label: ds.label, color: ds.borderColor, x: p.x, y: p.y });
        break;
      }
    });
    if (marks.length < 2) return;

    // Not enough vertical room to separate them all — the legend still carries identity.
    if ((marks.length - 1) * GAP > area.bottom - area.top - 12) return;

    marks.sort((a, b) => a.y - b.y);
    let cursor = area.top + 6;
    for (const m of marks) {
      m.y = Math.max(m.y - 10, cursor);
      cursor = m.y + GAP;
    }
    // Pull the stack back inside if the last one overflowed the bottom.
    const overflow = marks[marks.length - 1].y - (area.bottom - 6);
    if (overflow > 0) for (const m of marks) m.y -= overflow;

    ctx.save();
    ctx.font = '11px system-ui, -apple-system, "Segoe UI", sans-serif';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    // Halo in the surface color so a label stays legible where it crosses a line.
    ctx.lineJoin = 'round';
    ctx.lineWidth = 3;
    ctx.strokeStyle = INK.surface;
    for (const m of marks) {
      const lx = Math.min(m.x, area.right - 3);
      ctx.strokeText(m.label, lx, m.y);
      ctx.fillStyle = m.color;
      ctx.fillText(m.label, lx, m.y);
    }
    ctx.restore();
  },
};
Chart.register(directLabels);

@Component({
  selector: 'app-history',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="head">
      <h1>History</h1>
      <div class="controls">
        <div class="seg" role="group" aria-label="Chart mode">
          <button [class.on]="mode === 'per-node'" (click)="setMode('per-node')">Per node</button>
          <button [class.on]="mode === 'per-metric'" (click)="setMode('per-metric')">All nodes</button>
        </div>

        @if (mode === 'per-node') {
          <select [(ngModel)]="device" (change)="reload()" aria-label="Node">
            @for (d of devices; track d.mqtt_device_id) {
              <option [value]="d.mqtt_device_id">{{ d.friendly_name || d.mqtt_device_id }}</option>
            }
          </select>
        } @else {
          <select [(ngModel)]="metricKey" (change)="rebuild()" aria-label="Metric">
            @for (m of chartable; track m.key) {
              <option [value]="m.key">{{ m.fullLabel || m.label }}</option>
            }
          </select>
        }

        <select [(ngModel)]="hours" (change)="reload()" aria-label="Time range">
          <option [ngValue]="6">6 h</option>
          <option [ngValue]="24">24 h</option>
          <option [ngValue]="168">7 d</option>
          <option [ngValue]="720">30 d</option>
        </select>
      </div>
    </div>

    @if (mode === 'per-node') {
      <div class="card picker">
        <div class="picker-head">
          <span class="muted">Metrics</span>
          <button class="linky" (click)="resetMetrics()">reset</button>
        </div>
        <div class="chips">
          @for (m of chartable; track m.key) {
            <label class="chip" [class.on]="selected.has(m.key)">
              <input type="checkbox" [checked]="selected.has(m.key)" (change)="toggle(m.key)">
              {{ m.label }}<span class="chip-group">{{ m.group }}</span>
            </label>
          }
        </div>
      </div>
    }

    @if (error) { <div class="card"><span class="badge bad">error</span> {{ error }}</div> }
    @if (loading) { <div class="card"><p class="muted">Loading…</p></div> }
    @if (!loading && !error && !blocks.length) {
      <div class="card"><p class="muted">
        No stored samples in this range. hms-nut writes one row per device every
        COLLECTOR_SAVE_INTERVAL (default 1 h), so a fresh service has little history yet.
      </p></div>
    }

    @for (b of blocks; track b.id) {
      <div class="card">
        <h3 class="chart-title">{{ b.title }}</h3>
        <div class="chart-wrap"><canvas #chartCanvas></canvas></div>
      </div>
    }

    @if (stats.length) {
      <div class="card">
        <h3 class="chart-title">{{ mode === 'per-node' ? 'Metric statistics' : 'Per-node statistics' }}
          <span class="muted">· {{ rangeLabel() }}</span></h3>
        <table class="stats">
          <thead>
            <tr>
              <th>{{ mode === 'per-node' ? 'Metric' : 'Node' }}</th>
              <th class="num">Min</th><th class="num">Avg</th><th class="num">Max</th>
              <th class="num">Last</th><th class="num">Samples</th>
            </tr>
          </thead>
          <tbody>
            @for (s of stats; track s.name) {
              <tr>
                <td><span class="swatch" [style.background]="s.color"></span>{{ s.name }}</td>
                <td class="num">{{ s.min }}</td>
                <td class="num">{{ s.avg }}</td>
                <td class="num">{{ s.max }}</td>
                <td class="num">{{ s.last }}</td>
                <td class="num muted">{{ s.samples }}</td>
              </tr>
            }
          </tbody>
        </table>
      </div>
    }
  `,
  styles: [`
    .head { display:flex; align-items:center; justify-content:space-between; margin-bottom:14px; flex-wrap:wrap; gap:10px; }
    h1 { margin:0; }
    .controls { display:flex; gap:10px; flex-wrap:wrap; }
    .controls select { width:auto; min-width:150px; }

    .seg { display:flex; border:1px solid var(--border); border-radius:6px; overflow:hidden; }
    .seg button { background:var(--card-2); color:var(--muted); border:none; border-radius:0;
                  padding:8px 14px; font-size:14px; }
    .seg button.on { background:var(--primary); color:#fff; }

    .picker-head { display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; }
    .chips { display:flex; flex-wrap:wrap; gap:6px; }
    .chip { display:inline-flex; align-items:center; gap:6px; cursor:pointer;
            padding:5px 10px; border:1px solid var(--border); border-radius:999px;
            font-size:13px; color:var(--muted); background:var(--card-2); }
    .chip.on { color:var(--text); border-color:var(--primary); }
    .chip input { width:auto; margin:0; }
    .chip-group { font-size:11px; opacity:.6; }
    .linky { background:none; border:none; color:var(--accent); padding:0; font-size:13px; cursor:pointer; }

    .chart-title { margin:0 0 10px; font-size:14px; }
    .chart-wrap { position:relative; height:280px; }

    table.stats td, table.stats th { padding:7px 10px; }
    table.stats .num { text-align:right; font-variant-numeric: tabular-nums; }
    .swatch { display:inline-block; width:10px; height:10px; border-radius:2px;
              margin-right:8px; vertical-align:middle; }
  `],
})
export class HistoryComponent implements OnInit, AfterViewInit, OnDestroy {
  @ViewChildren('chartCanvas') canvases!: QueryList<ElementRef<HTMLCanvasElement>>;

  devices: DeviceStatus[] = [];
  mode: Mode = 'per-node';
  device = '';
  metricKey = 'load_percentage';
  hours = 24;
  loading = false;
  error = '';

  chartable: MetricDef[] = CHARTED_METRICS;
  selected = new Set<string>(DEFAULT_CHART_KEYS);
  blocks: ChartBlock[] = [];
  stats: StatRow[] = [];

  private series: HistorySeries[] = [];
  private charts: Chart[] = [];
  private viewReady = false;

  constructor(private api: NutApiService) {}

  ngOnInit() {
    this.api.getDevices().subscribe({
      next: d => {
        this.devices = d;
        if (!this.device && d.length) this.device = d[0].mqtt_device_id;
        this.reload();
      },
      error: e => this.error = e?.message || 'failed to load devices',
    });
  }

  ngAfterViewInit() {
    this.viewReady = true;
    // Canvases appear and disappear as blocks change; bind charts each time.
    this.canvases.changes.subscribe(() => this.paint());
    this.paint();
  }

  ngOnDestroy() { this.destroyCharts(); }

  setMode(m: Mode) {
    if (this.mode === m) return;
    this.mode = m;
    this.reload();
  }

  toggle(key: string) {
    if (this.selected.has(key)) this.selected.delete(key);
    else this.selected.add(key);
    this.rebuild();
  }

  resetMetrics() {
    this.selected = new Set<string>(DEFAULT_CHART_KEYS);
    this.rebuild();
  }

  rangeLabel(): string {
    if (this.hours <= 24) return `last ${this.hours} h`;
    return `last ${Math.round(this.hours / 24)} d`;
  }

  /** Fetch: one device in per-node mode, every device in per-metric mode. */
  reload() {
    const ids = this.mode === 'per-node'
      ? (this.device ? [this.device] : [])
      : this.devices.map(d => d.mqtt_device_id);

    if (!ids.length) { this.series = []; this.rebuild(); return; }

    this.loading = true;
    this.api.getHistory(ids, this.hours).subscribe({
      next: r => {
        this.series = r.series || [];
        this.loading = false;
        this.error = '';
        this.rebuild();
      },
      error: e => {
        this.loading = false;
        this.error = e?.message || 'failed to load history';
        this.series = [];
        this.rebuild();
      },
    });
  }

  /** Rebuild chart blocks + stats from the data already in hand. */
  rebuild() {
    this.blocks = this.mode === 'per-node' ? this.buildPerNode() : this.buildPerMetric();
    this.stats = this.mode === 'per-node' ? this.statsPerNode() : this.statsPerMetric();
    // Blocks may be unchanged in count, in which case QueryList never fires.
    setTimeout(() => this.paint(), 0);
  }

  // ── per-node: one node, metrics bucketed by unit so no chart needs two axes ──

  private buildPerNode(): ChartBlock[] {
    const s = this.series[0];
    if (!s || !s.points.length) return [];

    const chosen = this.chartable.filter(m => this.selected.has(m.key));
    const blocks: ChartBlock[] = [];

    for (const axis of AXIS_ORDER) {
      const defs = chosen.filter(m => m.axis === axis && this.hasData(s, m.key));
      if (!defs.length) continue;

      const datasets = defs.map((def, i) =>
        this.dataset(seriesName(def), s, def, seriesColor(i), def.reference));
      blocks.push({
        id: `axis-${axis}`,
        title: AXIS_LABELS[axis as MetricAxis],
        unit: chartUnit(defs[0]),
        yMin: axis === 'pct' ? 0 : undefined,
        yMax: axis === 'pct' ? 100 : undefined,
        data: { datasets },
      });
    }
    return blocks;
  }

  // ── per-metric: one metric, every node overlaid on a single axis ──

  private buildPerMetric(): ChartBlock[] {
    const def = METRIC_BY_KEY[this.metricKey];
    if (!def) return [];

    const withData = this.series.filter(s => this.hasData(s, def.key));
    if (!withData.length) return [];

    // Color follows the node's position in the configured device list, so
    // adding or removing a node never repaints the others.
    const datasets = withData.map(s => {
      const idx = this.devices.findIndex(d => d.mqtt_device_id === s.device);
      return this.dataset(s.friendly_name || s.device, s, def, seriesColor(idx < 0 ? 0 : idx));
    });

    return [{
      id: `metric-${def.key}`,
      title: `${seriesName(def)} — ${AXIS_LABELS[def.axis as MetricAxis]}`,
      unit: chartUnit(def),
      yMin: def.axis === 'pct' ? 0 : undefined,
      yMax: def.axis === 'pct' ? 100 : undefined,
      data: { datasets },
    }];
  }

  private dataset(label: string, s: HistorySeries, def: MetricDef, color: string, dashed = false) {
    return {
      label,
      data: s.points.map(p => ({
        x: Date.parse(p.t),
        y: chartValue(def, p[def.key] as number | null),
      })),
      borderColor: color,
      backgroundColor: color,
      borderWidth: 2,
      borderDash: dashed ? [4, 4] : undefined,
      tension: 0.25,
      pointRadius: 0,
      pointHoverRadius: 4,
      spanGaps: false,
    };
  }

  private hasData(s: HistorySeries, key: string): boolean {
    return s.points.some(p => typeof p[key] === 'number');
  }

  // ── statistics (the table view of the same numbers) ──

  private statsPerNode(): StatRow[] {
    const s = this.series[0];
    if (!s) return [];
    const chosen = this.chartable.filter(m => this.selected.has(m.key) && this.hasData(s, m.key));
    // Row color must match the chart the series appears in — slots are assigned
    // per axis bucket, so recompute the position within each bucket.
    const rows: StatRow[] = [];
    for (const axis of AXIS_ORDER) {
      const defs = chosen.filter(m => m.axis === axis);
      defs.forEach((def, i) => {
        const row = this.statRow(seriesName(def), seriesColor(i), s, def);
        if (row) rows.push(row);
      });
    }
    return rows;
  }

  private statsPerMetric(): StatRow[] {
    const def = METRIC_BY_KEY[this.metricKey];
    if (!def) return [];
    const rows: StatRow[] = [];
    for (const s of this.series) {
      if (!this.hasData(s, def.key)) continue;
      const idx = this.devices.findIndex(d => d.mqtt_device_id === s.device);
      const row = this.statRow(s.friendly_name || s.device, seriesColor(idx < 0 ? 0 : idx), s, def);
      if (row) rows.push(row);
    }
    return rows;
  }

  private statRow(name: string, color: string, s: HistorySeries, def: MetricDef): StatRow | null {
    const vals = s.points
      .map(p => p[def.key])
      .filter((v): v is number => typeof v === 'number');
    if (!vals.length) return null;

    const sum = vals.reduce((a, b) => a + b, 0);
    return {
      name,
      color,
      unit: def.unit,
      min: formatMetric(def, Math.min(...vals)),
      avg: formatMetric(def, sum / vals.length),
      max: formatMetric(def, Math.max(...vals)),
      last: formatMetric(def, vals[vals.length - 1]),
      samples: vals.length,
    };
  }

  // ── chart binding ──

  private paint() {
    if (!this.viewReady || !this.canvases) return;
    this.destroyCharts();
    const els = this.canvases.toArray();
    this.blocks.forEach((b, i) => {
      const el = els[i];
      if (!el) return;
      const opts = lineChartOptions('', this.hours, {
        yMin: b.yMin,
        yMax: b.yMax,
        showLegend: b.data.datasets.length > 1,
        tooltipUnit: b.unit,
      });
      // Room on the right for the direct labels.
      opts.layout = { padding: { right: 8, top: 4 } };
      this.charts.push(new Chart(el.nativeElement, { type: 'line', data: b.data, options: opts }));
    });
  }

  private destroyCharts() {
    for (const c of this.charts) c.destroy();
    this.charts = [];
  }
}
