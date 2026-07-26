import { Component, ElementRef, OnDestroy, OnInit, ViewChild, AfterViewInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Chart } from 'chart.js/auto';
import { NutApiService, DeviceStatus } from '../services/nut-api.service';

@Component({
  selector: 'app-history',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="head">
      <h1>History</h1>
      <div class="controls">
        <select [(ngModel)]="device" (change)="reload()">
          @for (d of devices; track d.mqtt_device_id) {
            <option [value]="d.mqtt_device_id">{{ d.friendly_name || d.mqtt_device_id }}</option>
          }
        </select>
        <select [(ngModel)]="hours" (change)="reload()">
          <option [ngValue]="6">6 h</option>
          <option [ngValue]="24">24 h</option>
          <option [ngValue]="168">7 d</option>
          <option [ngValue]="720">30 d</option>
        </select>
      </div>
    </div>

    @if (error) { <div class="card"><span class="badge bad">error</span> {{ error }}</div> }
    <div class="card">
      @if (!points && !error) { <p class="muted">Loading…</p> }
      @if (points === 0) { <p class="muted">No data in this range.</p> }
      <canvas #chart></canvas>
    </div>
  `,
  styles: [`
    .head { display:flex; align-items:center; justify-content:space-between; margin-bottom:14px; flex-wrap:wrap; gap:10px; }
    h1 { margin:0; }
    .controls { display:flex; gap:10px; }
    .controls select { width:auto; min-width:150px; }
    canvas { max-height: 420px; }
  `],
})
export class HistoryComponent implements OnInit, AfterViewInit, OnDestroy {
  @ViewChild('chart') canvas!: ElementRef<HTMLCanvasElement>;
  devices: DeviceStatus[] = [];
  device = '';
  hours = 24;
  points: number | null = null;
  error = '';
  private chart?: Chart;
  private viewReady = false;

  constructor(private api: NutApiService) {}

  ngOnInit() {
    this.api.getDevices().subscribe({
      next: d => {
        this.devices = d;
        if (!this.device && d.length) { this.device = d[0].mqtt_device_id; this.reload(); }
      },
      error: e => this.error = e?.message || 'failed to load devices',
    });
  }
  ngAfterViewInit() { this.viewReady = true; if (this.device) this.reload(); }
  ngOnDestroy() { this.chart?.destroy(); }

  reload() {
    if (!this.device || !this.viewReady) return;
    this.api.getHistory(this.device, this.hours).subscribe({
      next: r => { this.points = r.points.length; this.draw(r.points); this.error = ''; },
      error: e => this.error = e?.message || 'failed to load history',
    });
  }

  private draw(pts: any[]) {
    const labels = pts.map(p => p.t.replace('T', ' ').replace('Z', ''));
    const mk = (key: string) => pts.map(p => p[key]);
    const data = {
      labels,
      datasets: [
        { label: 'Battery %', data: mk('battery_charge'), borderColor: '#4caf50', yAxisID: 'pct', tension: .25, pointRadius: 0 },
        { label: 'Load %',    data: mk('load_percentage'), borderColor: '#667eea', yAxisID: 'pct', tension: .25, pointRadius: 0 },
        { label: 'Input V',   data: mk('input_voltage'),   borderColor: '#ffb300', yAxisID: 'v',   tension: .25, pointRadius: 0 },
      ],
    };
    const opts: any = {
      responsive: true,
      interaction: { mode: 'index', intersect: false },
      plugins: { legend: { labels: { color: '#e0e0e0' } } },
      scales: {
        x: { ticks: { color: '#a0a0c0', maxTicksLimit: 8 }, grid: { color: '#2a2d45' } },
        pct: { position: 'left', min: 0, max: 100, ticks: { color: '#a0a0c0' }, grid: { color: '#2a2d45' } },
        v: { position: 'right', ticks: { color: '#a0a0c0' }, grid: { drawOnChartArea: false } },
      },
    };
    if (this.chart) { this.chart.data = data as any; this.chart.update(); }
    else this.chart = new Chart(this.canvas.nativeElement, { type: 'line', data: data as any, options: opts });
  }
}
