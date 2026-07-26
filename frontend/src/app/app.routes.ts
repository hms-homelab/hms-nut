import { Routes } from '@angular/router';

export const routes: Routes = [
  { path: '', redirectTo: 'dashboard', pathMatch: 'full' },
  { path: 'dashboard', loadComponent: () => import('./components/dashboard.component').then(m => m.DashboardComponent) },
  { path: 'history',   loadComponent: () => import('./components/history.component').then(m => m.HistoryComponent) },
  { path: 'events',    loadComponent: () => import('./components/events.component').then(m => m.EventsComponent) },
  { path: 'devices',   loadComponent: () => import('./components/devices.component').then(m => m.DevicesComponent) },
  { path: '**', redirectTo: 'dashboard' },
];
