import {
  AfterViewInit, Component, ElementRef, Input, OnChanges, OnDestroy, ViewChild,
} from '@angular/core';
import { Chart } from 'chart.js/auto';
import { sparklineOptions } from './chart-theme';

/**
 * Bare trend line for a single metric inside a card — no axes, no legend, no
 * tooltip. The card's own label names the series, so it never carries identity
 * by color alone.
 */
@Component({
  selector: 'app-sparkline',
  standalone: true,
  template: `<canvas #cv></canvas>`,
  styles: [`
    :host { display:block; height:34px; }
    canvas { width:100% !important; height:34px !important; }
  `],
})
export class SparklineComponent implements AfterViewInit, OnChanges, OnDestroy {
  @ViewChild('cv') canvas!: ElementRef<HTMLCanvasElement>;
  @Input() values: (number | null)[] = [];
  @Input() color = '#3987e5';

  private chart?: Chart;

  ngAfterViewInit() { this.render(); }
  ngOnChanges() { this.render(); }
  ngOnDestroy() { this.chart?.destroy(); }

  private render() {
    if (!this.canvas) return;
    const data = {
      labels: this.values.map((_, i) => i),
      datasets: [{
        data: this.values,
        borderColor: this.color,
        borderWidth: 2,
        tension: 0.3,
        pointRadius: 0,
        spanGaps: false,
        fill: false,
      }],
    };
    if (this.chart) {
      this.chart.data = data as any;
      this.chart.update('none');
    } else {
      this.chart = new Chart(this.canvas.nativeElement, {
        type: 'line',
        data: data as any,
        options: sparklineOptions(),
      });
    }
  }
}
