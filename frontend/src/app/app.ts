import { Component } from '@angular/core';
import { RouterOutlet, RouterLink, RouterLinkActive } from '@angular/router';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [RouterOutlet, RouterLink, RouterLinkActive],
  template: `
    <header class="topbar">
      <div class="brand">🔋 HMS-NUT <span class="muted">UPS Monitor</span></div>
      <nav>
        <a routerLink="/dashboard" routerLinkActive="active">Dashboard</a>
        <a routerLink="/history"   routerLinkActive="active">History</a>
        <a routerLink="/events"    routerLinkActive="active">Events</a>
        <a routerLink="/devices"   routerLinkActive="active">Devices</a>
      </nav>
    </header>
    <main class="content">
      <router-outlet></router-outlet>
    </main>
  `,
  styles: [`
    .topbar {
      display: flex; align-items: center; justify-content: space-between;
      padding: 12px 20px; background: var(--card); border-bottom: 1px solid var(--border);
      position: sticky; top: 0; z-index: 10;
    }
    .brand { font-size: 18px; font-weight: 700; }
    .brand .muted { font-weight: 400; font-size: 14px; margin-left: 6px; }
    nav { display: flex; gap: 6px; }
    nav a {
      padding: 7px 14px; border-radius: 6px; color: var(--muted); font-weight: 500;
    }
    nav a:hover { color: var(--text); text-decoration: none; background: var(--card-2); }
    nav a.active { color: #fff; background: var(--primary); }
    .content { max-width: 1100px; margin: 22px auto; padding: 0 20px; }
    @media (max-width: 640px) {
      .topbar { flex-direction: column; gap: 10px; align-items: flex-start; }
    }
  `],
})
export class AppComponent {}
