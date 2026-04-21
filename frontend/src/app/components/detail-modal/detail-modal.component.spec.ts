import { ComponentRef } from '@angular/core'
import { ComponentFixture, TestBed } from '@angular/core/testing'
import { DetailModalComponent, type DetailField } from './detail-modal.component'

describe('DetailModalComponent', () => {
  let fixture: ComponentFixture<DetailModalComponent>
  let ref: ComponentRef<DetailModalComponent>

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DetailModalComponent],
    }).compileComponents()
    fixture = TestBed.createComponent(DetailModalComponent)
    ref = fixture.componentRef
  })

  const setInputs = (inputs: Partial<{
    kind: string
    title: string
    fields: DetailField[]
    timestampUs: number
    payload: unknown
  }>) => {
    for (const [k, v] of Object.entries(inputs)) ref.setInput(k, v)
    fixture.detectChanges()
  }

  it('should render title and kind', () => {
    setInputs({ kind: 'clack', title: 'epoch 42 · ANNOUNCE' })
    const host: HTMLElement = fixture.nativeElement
    expect(host.querySelector('.kind')?.textContent).toContain('clack')
    expect(host.querySelector('h3')?.textContent).toContain('epoch 42 · ANNOUNCE')
  })

  it('should render fields into a dl', () => {
    setInputs({
      kind: 'clack',
      title: 't',
      fields: [
        { label: 'epoch', value: 42, mono: true },
        { label: 'verb', value: 'CLAIM' },
      ],
    })
    const host = fixture.nativeElement as HTMLElement
    const rows = host.querySelectorAll<HTMLElement>('.cc-meta-row')
    expect(rows.length).toBe(2)
    expect(rows[0].querySelector('dt')?.textContent).toContain('epoch')
    expect(rows[0].querySelector('dd')?.textContent).toContain('42')
  })

  it('should pretty-print object payload as JSON', () => {
    setInputs({ kind: 'clack', title: 't', payload: { hello: 'world' } })
    const host = fixture.nativeElement as HTMLElement
    const pre = host.querySelector<HTMLElement>('.cc-json pre')
    expect(pre?.textContent).toContain('"hello"')
    expect(pre?.textContent).toContain('"world"')
  })

  it('should show nothing in the payload section when payload is empty string', () => {
    setInputs({ kind: 'clack', title: 't', payload: '' })
    expect(fixture.nativeElement.querySelector('.cc-json')).toBeNull()
  })

  it('should emit close on backdrop click', () => {
    const emits: void[] = []
    fixture.componentInstance.close.subscribe(() => emits.push(undefined))
    setInputs({ kind: 'clack', title: 't' })
    const host = fixture.nativeElement as HTMLElement
    const backdrop = host.querySelector<HTMLElement>('.cc-modal-backdrop')!
    backdrop.click()
    expect(emits.length).toBe(1)
  })

  it('should emit close on ESC keydown', () => {
    let emitted = false
    fixture.componentInstance.close.subscribe(() => (emitted = true))
    setInputs({ kind: 'clack', title: 't' })
    fixture.componentInstance.onEsc()
    expect(emitted).toBeTrue()
  })

  it('should NOT emit close when modal body is clicked (stopPropagation)', () => {
    let emitted = false
    fixture.componentInstance.close.subscribe(() => (emitted = true))
    setInputs({ kind: 'clack', title: 't' })
    const host = fixture.nativeElement as HTMLElement
    const inner = host.querySelector<HTMLElement>('.cc-modal')!
    inner.click()
    expect(emitted).toBeFalse()
  })
})
