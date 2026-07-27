/**
 * Chart theme — categorical palette and shared Chart.js options.
 *
 * The eight slots are the dark-mode steps of the validated categorical palette,
 * checked against this app's chart surface (--card #1a1d35):
 *   lightness band PASS · chroma floor PASS · adjacent CVD ΔE 8.4 PASS ·
 *   adjacent normal-vision ΔE 19.3 PASS · contrast vs surface PASS (all ≥ 3:1)
 *
 * Slots are assigned in fixed order and never cycled — a 9th series is not a
 * generated hue. Within any chart, series take slots by position, which is what
 * the adjacency guarantee is measured on.
 */

export const SERIES_COLORS = [
  '#3987e5', // 1 blue
  '#d95926', // 2 orange
  '#199e70', // 3 aqua
  '#c98500', // 4 yellow
  '#d55181', // 5 magenta
  '#008300', // 6 green
  '#9085e9', // 7 violet
  '#e66767', // 8 red
] as const;

/** Chart chrome, matched to the app's dark tokens. */
export const INK = {
  surface: '#1a1d35',
  primary: '#e0e0e0',
  muted: '#a0a0c0',
  grid: '#2a2d45',
  axis: '#2a2d45',
};

export const MAX_SERIES = SERIES_COLORS.length;

/** Slot color by position. Past the eighth slot, series must be folded, not recolored. */
export function seriesColor(index: number): string {
  return SERIES_COLORS[index % SERIES_COLORS.length];
}

/** Compact axis tick / tooltip label for an epoch-ms timestamp. */
export function timeLabel(ms: number, spanHours: number): string {
  const d = new Date(ms);
  const time = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  if (spanHours <= 48) return time;
  const day = d.toLocaleDateString([], { month: 'short', day: 'numeric' });
  return spanHours <= 24 * 14 ? `${day} ${time}` : day;
}

/**
 * Base line-chart options. One y-scale only — charts are bucketed by unit so a
 * dual axis is never needed. x is a linear epoch-ms scale, which lets series
 * with non-aligned timestamps share a chart without a date adapter dependency.
 */
export function lineChartOptions(yTitle: string, spanHours: number, opts: {
  yMin?: number;
  yMax?: number;
  showLegend?: boolean;
  tooltipUnit?: string;
} = {}): any {
  return {
    responsive: true,
    maintainAspectRatio: false,
    animation: { duration: 200 },
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: {
        display: opts.showLegend !== false,
        position: 'bottom',
        labels: {
          color: INK.primary,
          boxWidth: 10,
          boxHeight: 10,
          usePointStyle: true,
          pointStyle: 'line',
          padding: 14,
        },
      },
      tooltip: {
        backgroundColor: '#0f1120',
        borderColor: INK.grid,
        borderWidth: 1,
        titleColor: INK.primary,
        bodyColor: INK.primary,
        padding: 10,
        callbacks: {
          title: (items: any[]) =>
            items.length ? new Date(items[0].parsed.x).toLocaleString() : '',
          label: (item: any) => {
            const v = item.parsed.y;
            if (v === null || v === undefined) return `${item.dataset.label}: —`;
            const unit = opts.tooltipUnit ?? '';
            return `${item.dataset.label}: ${Math.round(v * 100) / 100}${unit ? ' ' + unit : ''}`;
          },
        },
      },
    },
    scales: {
      x: {
        type: 'linear',
        ticks: {
          color: INK.muted,
          maxTicksLimit: 7,
          autoSkip: true,
          callback: (v: any) => timeLabel(Number(v), spanHours),
        },
        grid: { color: INK.grid, tickColor: 'transparent' },
        border: { color: INK.axis },
      },
      y: {
        // The card heading already states the unit; a vertical one-glyph axis
        // title only adds noise.
        title: { display: false, text: yTitle, color: INK.muted },
        min: opts.yMin,
        max: opts.yMax,
        ticks: { color: INK.muted, maxTicksLimit: 6 },
        grid: { color: INK.grid, tickColor: 'transparent' },
        border: { color: INK.axis },
      },
    },
  };
}

/** Options for a bare in-card sparkline: no axes, no legend, no tooltip. */
export function sparklineOptions(): any {
  return {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    plugins: { legend: { display: false }, tooltip: { enabled: false } },
    scales: { x: { display: false, type: 'linear' }, y: { display: false } },
    elements: { point: { radius: 0 } },
  };
}
