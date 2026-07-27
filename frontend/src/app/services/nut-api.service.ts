import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';

/**
 * Live metrics as emitted by UpsData::toJson(). Every field is optional — the
 * collector only holds what the device actually reports over MQTT.
 */
export interface UpsMetrics {
  timestamp?: string;

  // Battery
  battery_charge?: number;
  battery_voltage?: number;
  battery_runtime?: number;
  battery_nominal_voltage?: number;
  battery_low_threshold?: number;
  battery_warning_threshold?: number;
  battery_type?: string;
  battery_mfr_date?: string;

  // Input
  input_voltage?: number;
  input_nominal_voltage?: number;
  high_voltage_transfer?: number;
  low_voltage_transfer?: number;
  input_sensitivity?: string;
  last_transfer_reason?: string;

  // Load & status
  load_percentage?: number;
  load_watts?: number;
  ups_status?: string;
  power_failure?: boolean;

  // UPS info
  ups_nominal_power?: number;
  beeper_status?: string;
  self_test_result?: string;
  firmware_version?: string;
  delay_shutdown?: number;
  timer_reboot?: number;
  timer_shutdown?: number;

  // Driver
  driver_name?: string;
  driver_version?: string;
  driver_state?: string;

  // Environment & output
  temperature?: number;
  output_voltage?: number;
  output_nominal_voltage?: number;

  [key: string]: number | string | boolean | undefined;
}

export interface DeviceStatus {
  mqtt_device_id: string;
  db_identifier: string;
  friendly_name: string;
  online: boolean;
  metrics: UpsMetrics | null;
}

/** One stored sample. Numeric fields are number-or-null so charts can gap. */
export interface HistoryPoint {
  t: string;
  ups_status: string | null;
  power_failure: boolean | null;
  [key: string]: number | string | boolean | null;
}

export interface HistorySeries {
  device: string;
  db_identifier: string;
  friendly_name: string;
  points: HistoryPoint[];
}

export interface HistoryResponse {
  hours: number;
  series: HistorySeries[];
  /** Back-compat mirrors of the first series. */
  device: string;
  points: HistoryPoint[];
}

export interface PowerEvent {
  device_name: string;
  device_identifier: string;
  event_type: string;
  timestamp: string;
  battery_start: number | null;
  battery_end: number | null;
  load: number | null;
}

export interface DeviceConfig {
  mqtt_device_id: string;
  db_identifier: string;
  friendly_name: string;
  enabled: boolean;
}

export interface DailySummary {
  date: string;
  summary: string;
  model: string;
  generated_at: string;
}

export interface SummariesResponse {
  enabled: boolean;
  summaries: DailySummary[];
}

@Injectable({ providedIn: 'root' })
export class NutApiService {
  constructor(private http: HttpClient) {}

  getDevices(): Observable<DeviceStatus[]> {
    return this.http.get<DeviceStatus[]>('/api/devices');
  }

  /** One or more devices in a single round trip; response always carries `series`. */
  getHistory(mqttIds: string | string[], hours: number): Observable<HistoryResponse> {
    const ids = Array.isArray(mqttIds) ? mqttIds : [mqttIds];
    const q = encodeURIComponent(ids.join(','));
    return this.http.get<HistoryResponse>(`/api/history?device=${q}&hours=${hours}`);
  }

  getEvents(mqttId?: string, limit = 100): Observable<PowerEvent[]> {
    const d = mqttId ? `&device=${encodeURIComponent(mqttId)}` : '';
    return this.http.get<PowerEvent[]>(`/api/events?limit=${limit}${d}`);
  }

  /** Persisted daily energy summaries, newest first. */
  getSummaries(limit = 14): Observable<SummariesResponse> {
    return this.http.get<SummariesResponse>(`/api/summaries?limit=${limit}`);
  }

  /** Generate a summary on demand (defaults to yesterday server-side). */
  generateSummary(date?: string): Observable<{ success: boolean; date: string; summary?: string; message?: string }> {
    const q = date ? `?date=${encodeURIComponent(date)}` : '';
    return this.http.post<{ success: boolean; date: string; summary?: string; message?: string }>(
      `/api/summary${q}`, {});
  }

  listConfigs(): Observable<DeviceConfig[]> {
    return this.http.get<DeviceConfig[]>('/api/config/devices');
  }

  addConfig(cfg: Partial<DeviceConfig>): Observable<DeviceConfig> {
    return this.http.post<DeviceConfig>('/api/config/devices', cfg);
  }

  updateConfig(id: string, cfg: Partial<DeviceConfig>): Observable<DeviceConfig> {
    return this.http.put<DeviceConfig>(`/api/config/devices/${encodeURIComponent(id)}`, cfg);
  }

  deleteConfig(id: string): Observable<{ deleted: boolean; mqtt_device_id: string }> {
    return this.http.delete<{ deleted: boolean; mqtt_device_id: string }>(`/api/config/devices/${encodeURIComponent(id)}`);
  }
}
