import { Component, OnDestroy, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { NutApiService, DeviceStatus } from '../services/nut-api.service';

@Component({
  selector: 'app-dashboard',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="head">
      <h1>Live Status</h1>
      <span class="muted">auto-refresh 5s · {{ devices.length }} device(s)</span>
    </div>

    @if (error) { <div class="card"><span class="badge bad">error</span> {{ error }}</div> }

    <div class="grid">
      @for (d of devices; track d.mqtt_device_id) {
        <div class="card">
          <div class="card-head">
            <h3>{{ d.friendly_name || d.mqtt_device_id }}</h3>
            @if (d.online) {
              <span class="badge" [class.ok]="isOnline(d)" [class.warn]="isOnBattery(d)"
                    [class.bad]="isLowBattery(d)">{{ statusLabel(d) }}</span>
            } @else {
              <span class="badge off">offline</span>
            }
          </div>
          <div class="muted mono">{{ d.mqtt_device_id }}</div>

          @if (d.metrics) {
            <div class="metric">
              <div class="metric-row"><span class="muted">Battery</span>
                <span class="val">{{ fmt(d.metrics.battery_charge, '%') }}</span></div>
              <div class="meter"><span [style.width.%]="d.metrics.battery_charge || 0"></span></div>
            </div>
            <div class="metric">
              <div class="metric-row"><span class="muted">Load</span>
                <span class="val">{{ fmt(d.metrics.load_percentage, '%') }}
                  @if (d.metrics.load_watts != null) { <span class="muted">({{ d.metrics.load_watts }} W)</span> }
                </span></div>
              <div class="meter"><span [style.width.%]="d.metrics.load_percentage || 0"></span></div>
            </div>
            <table class="mini">
              <tr><th>Input</th><td class="val">{{ fmt(d.metrics.input_voltage, ' V') }}</td></tr>
              <tr><th>Runtime</th><td class="val">{{ runtime(d.metrics.battery_runtime) }}</td></tr>
              @if (d.metrics.battery_voltage != null) {
                <tr><th>Battery V</th><td class="val">{{ fmt(d.metrics.battery_voltage, ' V') }}</td></tr>
              }
              @if (d.metrics.temperature != null) {
                <tr><th>Temp</th><td class="val">{{ fmt(d.metrics.temperature, ' °C') }}</td></tr>
              }
            </table>
          } @else {
            <p class="muted">No live data yet.</p>
          }
        </div>
      }
      @if (!devices.length && !error) { <p class="muted">Loading…</p> }
    </div>
  `,
  styles: [`
    .head { display:flex; align-items:baseline; justify-content:space-between; margin-bottom:14px; }
    h1 { margin:0; }
    .card-head { display:flex; align-items:center; justify-content:space-between; }
    .card-head h3 { margin:0 0 2px; }
    .mono { font-family: ui-monospace, monospace; font-size:12px; margin-bottom:10px; }
    .metric { margin:10px 0; }
    .metric-row { display:flex; justify-content:space-between; margin-bottom:4px; }
    table.mini { margin-top:12px; }
    table.mini th { width:45%; }
  `],
})
export class DashboardComponent implements OnInit, OnDestroy {
  devices: DeviceStatus[] = [];
  error = '';
  private timer: any;

  constructor(private api: NutApiService) {}

  ngOnInit() { this.load(); this.timer = setInterval(() => this.load(), 5000); }
  ngOnDestroy() { clearInterval(this.timer); }

  load() {
    this.api.getDevices().subscribe({
      next: d => { this.devices = d; this.error = ''; },
      error: e => this.error = e?.message || 'failed to load devices',
    });
  }

  fmt(v: number | null | undefined, unit: string): string {
    return v == null ? '—' : `${Math.round(v * 10) / 10}${unit}`;
  }
  runtime(sec: number | null | undefined): string {
    if (sec == null) return '—';
    const m = Math.round(sec / 60);
    return m >= 60 ? `${Math.floor(m / 60)}h ${m % 60}m` : `${m} min`;
  }
  private status(d: DeviceStatus): string { return (d.metrics?.ups_status || '').toUpperCase(); }
  isOnline(d: DeviceStatus): boolean { const s = this.status(d); return s.includes('OL') || s.includes('ONLINE'); }
  isOnBattery(d: DeviceStatus): boolean { const s = this.status(d); return s.includes('OB') || s.includes('BATTERY') || d.metrics?.power_failure === true; }
  isLowBattery(d: DeviceStatus): boolean { const s = this.status(d); return s.includes('LB') || (d.metrics?.battery_charge != null && d.metrics.battery_charge < 20); }
  statusLabel(d: DeviceStatus): string {
    if (this.isLowBattery(d)) return 'low battery';
    if (this.isOnBattery(d)) return 'on battery';
    if (this.isOnline(d)) return 'online';
    return d.metrics?.ups_status || 'unknown';
  }
}
