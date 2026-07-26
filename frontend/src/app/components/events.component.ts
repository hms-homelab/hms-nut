import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { NutApiService, DeviceStatus, PowerEvent } from '../services/nut-api.service';

@Component({
  selector: 'app-events',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="head">
      <h1>Power Events</h1>
      <select [(ngModel)]="device" (change)="load()">
        <option value="">All devices</option>
        @for (d of devices; track d.mqtt_device_id) {
          <option [value]="d.mqtt_device_id">{{ d.friendly_name || d.mqtt_device_id }}</option>
        }
      </select>
    </div>

    @if (error) { <div class="card"><span class="badge bad">error</span> {{ error }}</div> }
    <div class="card">
      @if (events === null) { <p class="muted">Loading…</p> }
      @else if (!events.length) { <p class="muted">No power events recorded.</p> }
      @else {
        <table>
          <thead><tr><th>Time (UTC)</th><th>Device</th><th>Event</th><th>Battery</th><th>Load</th></tr></thead>
          <tbody>
            @for (e of events; track $index) {
              <tr>
                <td class="mono">{{ e.timestamp.replace('T',' ').replace('Z','') }}</td>
                <td>{{ e.device_name }}</td>
                <td><span class="badge" [class.bad]="isOutage(e)" [class.ok]="isRestore(e)"
                          [class.warn]="!isOutage(e) && !isRestore(e)">{{ e.event_type }}</span></td>
                <td>{{ batt(e) }}</td>
                <td>{{ e.load == null ? '—' : (e.load + '%') }}</td>
              </tr>
            }
          </tbody>
        </table>
      }
    </div>
  `,
  styles: [`
    .head { display:flex; align-items:center; justify-content:space-between; margin-bottom:14px; gap:12px; flex-wrap:wrap; }
    h1 { margin:0; }
    .head select { width:auto; min-width:170px; }
    .mono { font-family: ui-monospace, monospace; font-size:12px; }
  `],
})
export class EventsComponent implements OnInit {
  devices: DeviceStatus[] = [];
  events: PowerEvent[] | null = null;
  device = '';
  error = '';

  constructor(private api: NutApiService) {}

  ngOnInit() {
    this.api.getDevices().subscribe({ next: d => this.devices = d });
    this.load();
  }

  load() {
    this.events = null;
    this.api.getEvents(this.device || undefined, 200).subscribe({
      next: e => { this.events = e; this.error = ''; },
      error: e => { this.error = e?.message || 'failed to load events'; this.events = []; },
    });
  }

  isOutage(e: PowerEvent): boolean { return /out|battery|fail|lb|ob/i.test(e.event_type); }
  isRestore(e: PowerEvent): boolean { return /end|restore|online|ol|return/i.test(e.event_type); }
  batt(e: PowerEvent): string {
    if (e.battery_start == null && e.battery_end == null) return '—';
    if (e.battery_end == null) return `${e.battery_start}%`;
    return `${e.battery_start}% → ${e.battery_end}%`;
  }
}
