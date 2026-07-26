import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { NutApiService, DeviceConfig } from '../services/nut-api.service';

@Component({
  selector: 'app-devices',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    <div class="head"><h1>Device Management</h1></div>
    <p class="muted">Changes apply live — the collector re-subscribes without a restart.</p>

    @if (error) { <div class="card"><span class="badge bad">error</span> {{ error }}</div> }
    @if (notice) { <div class="card"><span class="badge ok">ok</span> {{ notice }}</div> }

    <div class="card">
      <table>
        <thead><tr><th>MQTT device id</th><th>DB identifier</th><th>Friendly name</th><th>Enabled</th><th></th></tr></thead>
        <tbody>
          @for (c of configs; track c.mqtt_device_id) {
            <tr>
              <td class="mono">{{ c.mqtt_device_id }}</td>
              @if (editing === c.mqtt_device_id) {
                <td><input [(ngModel)]="edit.db_identifier"></td>
                <td><input [(ngModel)]="edit.friendly_name"></td>
                <td><input type="checkbox" [(ngModel)]="edit.enabled" class="chk"></td>
                <td class="actions">
                  <button (click)="save(c)">Save</button>
                  <button class="ghost" (click)="editing=''">Cancel</button>
                </td>
              } @else {
                <td class="mono">{{ c.db_identifier }}</td>
                <td>{{ c.friendly_name || '—' }}</td>
                <td><span class="badge" [class.ok]="c.enabled" [class.off]="!c.enabled">{{ c.enabled ? 'yes' : 'no' }}</span></td>
                <td class="actions">
                  <button class="ghost" (click)="startEdit(c)">Edit</button>
                  <button class="ghost" (click)="toggle(c)">{{ c.enabled ? 'Disable' : 'Enable' }}</button>
                  <button class="danger" (click)="remove(c)">Delete</button>
                </td>
              }
            </tr>
          }
          @if (!configs.length) { <tr><td colspan="5" class="muted">No devices configured.</td></tr> }
        </tbody>
      </table>
    </div>

    <div class="card">
      <h3>Add device</h3>
      <div class="add-grid">
        <div>
          <label class="muted">MQTT device id *</label>
          <input [(ngModel)]="add.mqtt_device_id" placeholder="apc_ups_e072a1ead480">
        </div>
        <div>
          <label class="muted">DB identifier</label>
          <input [(ngModel)]="add.db_identifier" placeholder="(defaults to MQTT id)">
        </div>
        <div>
          <label class="muted">Friendly name</label>
          <input [(ngModel)]="add.friendly_name" placeholder="Backup UPS — Hub">
        </div>
      </div>
      <button (click)="create()">Add device</button>
    </div>
  `,
  styles: [`
    .head h1 { margin:0 0 4px; }
    .mono { font-family: ui-monospace, monospace; font-size:12px; }
    .actions { display:flex; gap:6px; white-space:nowrap; }
    .actions button { padding:5px 10px; font-size:13px; }
    .chk { width:auto; }
    .add-grid { display:grid; grid-template-columns:repeat(auto-fit, minmax(200px,1fr)); gap:12px; margin-bottom:12px; }
    label { display:block; margin-bottom:4px; font-size:13px; }
  `],
})
export class DevicesComponent implements OnInit {
  configs: DeviceConfig[] = [];
  editing = '';
  edit: Partial<DeviceConfig> = {};
  add: Partial<DeviceConfig> = { mqtt_device_id: '', db_identifier: '', friendly_name: '' };
  error = '';
  notice = '';

  constructor(private api: NutApiService) {}

  ngOnInit() { this.load(); }

  load() {
    this.api.listConfigs().subscribe({
      next: c => { this.configs = c; this.error = ''; },
      error: e => this.error = e?.message || 'failed to load config',
    });
  }

  startEdit(c: DeviceConfig) { this.editing = c.mqtt_device_id; this.edit = { ...c }; }

  save(c: DeviceConfig) {
    this.api.updateConfig(c.mqtt_device_id, this.edit).subscribe({
      next: () => { this.editing = ''; this.flash('Updated ' + c.mqtt_device_id); this.load(); },
      error: e => this.error = e?.error?.error || e?.message || 'update failed',
    });
  }

  toggle(c: DeviceConfig) {
    this.api.updateConfig(c.mqtt_device_id, { ...c, enabled: !c.enabled }).subscribe({
      next: () => { this.flash((c.enabled ? 'Disabled ' : 'Enabled ') + c.mqtt_device_id); this.load(); },
      error: e => this.error = e?.error?.error || e?.message || 'toggle failed',
    });
  }

  remove(c: DeviceConfig) {
    if (!confirm(`Delete ${c.mqtt_device_id}? History is kept; the collector stops monitoring it.`)) return;
    this.api.deleteConfig(c.mqtt_device_id).subscribe({
      next: () => { this.flash('Deleted ' + c.mqtt_device_id); this.load(); },
      error: e => this.error = e?.error?.error || e?.message || 'delete failed',
    });
  }

  create() {
    if (!this.add.mqtt_device_id) { this.error = 'MQTT device id is required'; return; }
    this.api.addConfig(this.add).subscribe({
      next: () => {
        this.flash('Added ' + this.add.mqtt_device_id);
        this.add = { mqtt_device_id: '', db_identifier: '', friendly_name: '' };
        this.load();
      },
      error: e => this.error = e?.error?.error || e?.message || 'add failed',
    });
  }

  private flash(msg: string) { this.notice = msg; this.error = ''; setTimeout(() => this.notice = '', 4000); }
}
