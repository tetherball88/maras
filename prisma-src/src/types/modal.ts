export type PrismaModalTone = 'default' | 'warning' | 'danger' | 'success' | 'ai'

export type PrismaModalSize = 'sm' | 'md' | 'lg' | 'xl'

export type PrismaModalButtonVariant = 'default' | 'secondary' | 'outline' | 'ghost' | 'danger'

export type PrismaModalControlType = 'checkbox' | 'select' | 'text' | 'checklist' | 'infobox'

export interface PrismaModalSelectOption {
  value: string
  label: string
  disabled?: boolean
  tooltip?: string
}

export interface PrismaModalChecklistItem {
  id: string
  label: string
  subtitle?: string
  description?: string
  badgeText?: string
  badgeTone?: 'default' | 'accent' | 'warning' | 'danger' | 'success'
  checked?: boolean
  checkboxDisabled?: boolean
  selectKey?: string
  selectLabel?: string
  selectValue?: string
  selectOptions?: PrismaModalSelectOption[]
  meta?: Record<string, any>
}

export interface PrismaModalControl {
  type: PrismaModalControlType
  id: string
  label?: string
  description?: string
  required?: boolean
  disabled?: boolean
  defaultValue?: boolean | string | number | null
  placeholder?: string
  options?: PrismaModalSelectOption[]
  items?: PrismaModalChecklistItem[]
  showSelectAllToggle?: boolean
  emptyMessage?: string
  tone?: PrismaModalTone
}

export interface PrismaModalButton {
  id: string
  label: string
  variant?: PrismaModalButtonVariant
  disabled?: boolean
  tooltip?: string
  requireChecklistId?: string
  minCheckedCount?: number
}

export interface PrismaModalPayload {
  modalId: string
  modalType?: string
  title: string
  subtitle?: string
  message?: string
  tone?: PrismaModalTone
  size?: PrismaModalSize
  controls?: PrismaModalControl[]
  buttons: PrismaModalButton[]
  customData?: Record<string, any>
}

export interface PrismaModalChecklistItemState {
  checked: boolean
  selectValue?: string
}

export interface PrismaModalChecklistValue {
  checkedIds: string[]
  uncheckedIds: string[]
  items: Record<string, PrismaModalChecklistItemState>
}

export interface PrismaModalResponsePayload {
  modalId: string
  buttonId: string
  buttonIndex: number
  values: Record<string, any>
}

