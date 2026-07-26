import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';

export interface UpsMetrics {
  battery_charge?: number;
  battery_voltage?: number;
  battery_runtime?: number;
  input_voltage?: number;
  load_percentage?: number;
  load_watts?: number;
  ups_status?: string;
  power_failure?: boolean;
  temperature?: number;
  timestamp?: string;
}

export interface DeviceStatus {
  mqtt_device_id: string;
  db_identifier: string;
  friendly_name: string;
  online: boolean;
  metrics: UpsMetrics | null;
}

export interface HistoryPoint {
  t: string;
  battery_charge: number | null;
  load_percentage: number | null;
  input_voltage: number | null;
}

export interface HistoryResponse {
  device: string;
  hours: number;
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

@Injectable({ providedIn: 'root' })
export class NutApiService {
  constructor(private http: HttpClient) {}

  getDevices(): Observable<DeviceStatus[]> {
    return this.http.get<DeviceStatus[]>('/api/devices');
  }

  getHistory(mqttId: string, hours: number): Observable<HistoryResponse> {
    return this.http.get<HistoryResponse>(`/api/history?device=${encodeURIComponent(mqttId)}&hours=${hours}`);
  }

  getEvents(mqttId?: string, limit = 100): Observable<PowerEvent[]> {
    const d = mqttId ? `&device=${encodeURIComponent(mqttId)}` : '';
    return this.http.get<PowerEvent[]>(`/api/events?limit=${limit}${d}`);
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
