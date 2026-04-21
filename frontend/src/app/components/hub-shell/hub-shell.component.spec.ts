import { TestBed } from '@angular/core/testing'
import { provideRouter } from '@angular/router'
import { provideZonelessChangeDetection } from '@angular/core'
import { HubShellComponent } from './hub-shell.component'
import { HubStateService } from '../../services/hub-state.service'
import { WsService } from '../../services/ws.service'
import { mockHubState, mockWsService } from '../../../testing/mocks'

describe('HubShellComponent', () => {
  let hub: ReturnType<typeof mockHubState>
  let ws: ReturnType<typeof mockWsService>

  beforeEach(() => {
    document.documentElement.removeAttribute('data-theme')
    localStorage.clear()
    hub = mockHubState()
    ws = mockWsService()
    TestBed.configureTestingModule({
      imports: [HubShellComponent],
      providers: [
        provideZonelessChangeDetection(),
        provideRouter([]),
        { provide: HubStateService, useValue: hub },
        { provide: WsService, useValue: ws },
      ],
    })
  })

  it('initialises the hub state on ngOnInit', () => {
    const fx = TestBed.createComponent(HubShellComponent)
    fx.detectChanges()
    expect(hub.init).toHaveBeenCalled()
  })

  it('defaults theme to dark', () => {
    const fx = TestBed.createComponent(HubShellComponent)
    expect(fx.componentInstance.theme()).toBe('dark')
  })

  it('reads theme from <html data-theme="light">', () => {
    document.documentElement.setAttribute('data-theme', 'light')
    const fx = TestBed.createComponent(HubShellComponent)
    expect(fx.componentInstance.theme()).toBe('light')
  })

  it('toggles theme and persists selection', () => {
    const fx = TestBed.createComponent(HubShellComponent)
    const cmp = fx.componentInstance
    cmp.toggleTheme()
    expect(cmp.theme()).toBe('light')
    expect(document.documentElement.getAttribute('data-theme')).toBe('light')
    expect(localStorage.getItem('cc-theme')).toBe('light')

    cmp.toggleTheme()
    expect(cmp.theme()).toBe('dark')
    expect(localStorage.getItem('cc-theme')).toBe('dark')
  })
})
