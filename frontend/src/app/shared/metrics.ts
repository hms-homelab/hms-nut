/**
 * Metric catalog — the single vocabulary shared by the live dashboard and the
 * history charts.
 *
 * Keys match the JSON emitted by UpsData::toJson() (live) and
 * DatabaseService::queryHistory() (stored). The history query aliases its two
 * threshold columns to the live names so both surfaces speak the same language.
 */

export type MetricGroup =
  | 'Battery'
  | 'Input'
  | 'Output'
  | 'Load'
  | 'UPS'
  | 'Driver'
  | 'Environment';

/** Unit family. Charts are bucketed by axis so no chart ever needs two y-scales. */
export type MetricAxis = 'pct' | 'volt_ac' | 'volt_dc' | 'watt' | 'minutes' | 'temp';

export interface MetricDef {
  key: string;
  /** Short name, used inside its own group's section. */
  label: string;
  /**
   * Self-contained name, used wherever metrics from different groups sit side
   * by side (chart series, legends, stat rows) — three different metrics are
   * all called "Voltage" within their groups.
   */
  fullLabel?: string;
  unit: string;
  group: MetricGroup;
  /** Present in history rows, so it can be charted over time. */
  charted?: boolean;
  axis?: MetricAxis;
  /** Rendered as a dashed reference line rather than a primary series. */
  reference?: boolean;
  /** Fixed nameplate value — shown in details, never charted as a trend. */
  nameplate?: boolean;
  decimals?: number;
  /** Seconds in the payload, displayed as a duration. */
  duration?: boolean;
}

export const AXIS_LABELS: Record<MetricAxis, string> = {
  pct: 'Percent (%)',
  volt_ac: 'AC volts (V)',
  volt_dc: 'Battery volts (V)',
  watt: 'Power (W)',
  minutes: 'Runtime (min)',
  temp: 'Temperature (°C)',
};

export const METRICS: MetricDef[] = [
  // ── Battery ──
  { key: 'battery_charge', label: 'Charge', fullLabel: 'Battery charge', unit: '%', group: 'Battery', charted: true, axis: 'pct', decimals: 1 },
  { key: 'battery_voltage', label: 'Voltage', fullLabel: 'Battery voltage', unit: 'V', group: 'Battery', charted: true, axis: 'volt_dc', decimals: 2 },
  { key: 'battery_runtime', label: 'Runtime', fullLabel: 'Battery runtime', unit: 's', group: 'Battery', charted: true, axis: 'minutes', duration: true },
  { key: 'battery_nominal_voltage', label: 'Nominal voltage', fullLabel: 'Battery nominal voltage', unit: 'V', group: 'Battery', charted: true, axis: 'volt_dc', reference: true, nameplate: true, decimals: 1 },
  { key: 'battery_low_threshold', label: 'Low threshold', fullLabel: 'Battery low threshold', unit: '%', group: 'Battery', charted: true, axis: 'pct', reference: true, decimals: 0 },
  { key: 'battery_warning_threshold', label: 'Warning threshold', fullLabel: 'Battery warning threshold', unit: '%', group: 'Battery', charted: true, axis: 'pct', reference: true, decimals: 0 },
  { key: 'battery_type', label: 'Type', fullLabel: 'Battery type', unit: '', group: 'Battery' },
  { key: 'battery_mfr_date', label: 'Manufactured', fullLabel: 'Battery manufactured', unit: '', group: 'Battery' },

  // ── Input ──
  { key: 'input_voltage', label: 'Voltage', fullLabel: 'Input voltage', unit: 'V', group: 'Input', charted: true, axis: 'volt_ac', decimals: 1 },
  { key: 'input_nominal_voltage', label: 'Nominal voltage', fullLabel: 'Input nominal voltage', unit: 'V', group: 'Input', charted: true, axis: 'volt_ac', reference: true, nameplate: true, decimals: 0 },
  { key: 'high_voltage_transfer', label: 'Transfer high', fullLabel: 'Transfer high', unit: 'V', group: 'Input', charted: true, axis: 'volt_ac', reference: true, decimals: 0 },
  { key: 'low_voltage_transfer', label: 'Transfer low', fullLabel: 'Transfer low', unit: 'V', group: 'Input', charted: true, axis: 'volt_ac', reference: true, decimals: 0 },
  { key: 'input_sensitivity', label: 'Sensitivity', fullLabel: 'Input sensitivity', unit: '', group: 'Input' },
  { key: 'last_transfer_reason', label: 'Last transfer reason', fullLabel: 'Last transfer reason', unit: '', group: 'Input' },

  // ── Output ──
  { key: 'output_voltage', label: 'Voltage', fullLabel: 'Output voltage', unit: 'V', group: 'Output', charted: true, axis: 'volt_ac', decimals: 1 },
  { key: 'output_nominal_voltage', label: 'Nominal voltage', fullLabel: 'Output nominal voltage', unit: 'V', group: 'Output', charted: true, axis: 'volt_ac', reference: true, nameplate: true, decimals: 0 },

  // ── Load ──
  { key: 'load_percentage', label: 'Load', fullLabel: 'Load', unit: '%', group: 'Load', charted: true, axis: 'pct', decimals: 1 },
  { key: 'load_watts', label: 'Power draw', fullLabel: 'Power draw', unit: 'W', group: 'Load', charted: true, axis: 'watt', decimals: 0 },

  // ── UPS ──
  { key: 'ups_status', label: 'Status', fullLabel: 'UPS status', unit: '', group: 'UPS' },
  { key: 'power_failure', label: 'Power failure', fullLabel: 'Power failure', unit: '', group: 'UPS' },
  { key: 'ups_nominal_power', label: 'Nominal power', fullLabel: 'Nominal power', unit: 'W', group: 'UPS', nameplate: true, decimals: 0 },
  { key: 'beeper_status', label: 'Beeper', fullLabel: 'Beeper', unit: '', group: 'UPS' },
  { key: 'self_test_result', label: 'Self test', fullLabel: 'Self test', unit: '', group: 'UPS' },
  { key: 'firmware_version', label: 'Firmware', fullLabel: 'Firmware', unit: '', group: 'UPS' },
  { key: 'delay_shutdown', label: 'Shutdown delay', fullLabel: 'Shutdown delay', unit: 's', group: 'UPS', duration: true },
  { key: 'timer_reboot', label: 'Reboot timer', fullLabel: 'Reboot timer', unit: 's', group: 'UPS', duration: true },
  { key: 'timer_shutdown', label: 'Shutdown timer', fullLabel: 'Shutdown timer', unit: 's', group: 'UPS', duration: true },

  // ── Driver ──
  { key: 'driver_name', label: 'Name', fullLabel: 'Driver name', unit: '', group: 'Driver' },
  { key: 'driver_version', label: 'Version', fullLabel: 'Driver version', unit: '', group: 'Driver' },
  { key: 'driver_state', label: 'State', fullLabel: 'Driver state', unit: '', group: 'Driver' },

  // ── Environment ──
  { key: 'temperature', label: 'Temperature', fullLabel: 'Temperature', unit: '°C', group: 'Environment', charted: true, axis: 'temp', decimals: 1 },
];

export const GROUP_ORDER: MetricGroup[] = [
  'Battery', 'Input', 'Output', 'Load', 'UPS', 'Driver', 'Environment',
];

export const METRIC_BY_KEY: Record<string, MetricDef> =
  METRICS.reduce((acc, m) => { acc[m.key] = m; return acc; }, {} as Record<string, MetricDef>);

/** Metrics that can be plotted over time. */
export const CHARTED_METRICS = METRICS.filter(m => m.charted);

/** Chart-axis buckets, in display order — one chart per bucket, one y-scale each. */
export const AXIS_ORDER: MetricAxis[] = ['pct', 'volt_ac', 'watt', 'minutes', 'volt_dc', 'temp'];

/** Metrics the per-node view charts by default (the ones worth a first look). */
export const DEFAULT_CHART_KEYS = [
  'battery_charge', 'load_percentage', 'input_voltage', 'load_watts',
  'battery_runtime', 'battery_voltage', 'output_voltage', 'temperature',
];

/** Format a metric value for display, respecting units and duration fields. */
export function formatMetric(def: MetricDef | undefined, value: unknown): string {
  if (value === null || value === undefined || value === '') return '—';
  if (typeof value === 'boolean') return value ? 'yes' : 'no';
  if (!def) return String(value);

  if (typeof value === 'number') {
    if (def.duration) return formatDuration(value);
    const d = def.decimals ?? 1;
    const rounded = value.toFixed(d);
    return def.unit ? `${rounded}${def.unit === '%' || def.unit === '°C' ? '' : ' '}${def.unit}` : rounded;
  }
  return String(value);
}

/** Seconds → "1h 12m" / "46 min" / "38 s". */
export function formatDuration(seconds: number): string {
  if (!isFinite(seconds)) return '—';
  if (seconds < 60) return `${Math.round(seconds)} s`;
  const m = Math.round(seconds / 60);
  if (m < 60) return `${m} min`;
  return `${Math.floor(m / 60)}h ${m % 60}m`;
}

/** Chart-space value: runtime is charted in minutes, everything else as-is. */
export function chartValue(def: MetricDef, raw: number | null): number | null {
  if (raw === null || raw === undefined) return null;
  return def.duration ? raw / 60 : raw;
}

/** Unit of the charted value — duration metrics are plotted in minutes, not seconds. */
export function chartUnit(def: MetricDef): string {
  return def.duration ? 'min' : def.unit;
}

/** Name to use where metrics from different groups appear together. */
export function seriesName(def: MetricDef): string {
  return def.fullLabel || def.label;
}
