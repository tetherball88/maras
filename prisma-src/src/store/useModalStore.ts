import { create } from 'zustand'
import {
  PrismaModalPayload,
  PrismaModalControl,
  PrismaModalChecklistValue,
  PrismaModalChecklistItemState,
} from '../types/modal'
import { sendMarasAction } from './prismaBridge'
import { isInGame } from '../lib/utils'

interface ModalState {
  activeModal: PrismaModalPayload | null
  controlValues: Record<string, any>
  isSubmitting: boolean
  onResolve: ((buttonId: string, buttonIndex: number, values: Record<string, any>) => void) | null

  showModal: (
    modal: PrismaModalPayload,
    onResolve?: (buttonId: string, buttonIndex: number, values: Record<string, any>) => void,
  ) => void
  clearModal: () => void
  setControlValue: (controlId: string, value: any) => void
  toggleChecklistItem: (controlId: string, itemId: string, checked?: boolean) => void
  setAllChecklistItems: (controlId: string, checked: boolean) => void
  setChecklistItemSelectValue: (controlId: string, itemId: string, selectValue: string) => void
  submitModal: (buttonId: string, buttonIndex: number, customValues?: Record<string, any>) => void
}

function buildChecklistValueFromItems(
  control: PrismaModalControl,
  itemsMap: Record<string, PrismaModalChecklistItemState>,
): PrismaModalChecklistValue {
  const checkedIds: string[] = []
  const uncheckedIds: string[] = []
  const items = control.items || []

  for (const item of items) {
    const state = itemsMap[item.id]
    if (state?.checked) {
      checkedIds.push(item.id)
    } else {
      uncheckedIds.push(item.id)
    }
  }

  return {
    checkedIds,
    uncheckedIds,
    items: itemsMap,
  }
}

function initializeControlValues(controls?: PrismaModalControl[]): Record<string, any> {
  const initial: Record<string, any> = {}
  if (!controls) return initial

  for (const ctrl of controls) {
    if (!ctrl.id) continue

    switch (ctrl.type) {
      case 'checkbox':
        initial[ctrl.id] = Boolean(ctrl.defaultValue ?? false)
        break
      case 'select': {
        const firstOpt = ctrl.options?.[0]?.value ?? ''
        initial[ctrl.id] = ctrl.defaultValue !== undefined && ctrl.defaultValue !== null
          ? String(ctrl.defaultValue)
          : firstOpt
        break
      }
      case 'text':
        initial[ctrl.id] = ctrl.defaultValue !== undefined && ctrl.defaultValue !== null
          ? String(ctrl.defaultValue)
          : ''
        break
      case 'checklist': {
        const itemsMap: Record<string, PrismaModalChecklistItemState> = {}
        for (const item of ctrl.items || []) {
          const initialSelect =
            item.selectValue !== undefined
              ? item.selectValue
              : item.selectOptions?.[0]?.value
          itemsMap[item.id] = {
            checked: item.checked ?? true,
            ...(initialSelect !== undefined ? { selectValue: initialSelect } : {}),
          }
        }
        initial[ctrl.id] = buildChecklistValueFromItems(ctrl, itemsMap)
        break
      }
      default:
        break
    }
  }

  return initial
}

export const useModalStore = create<ModalState>((set, get) => ({
  activeModal: null,
  controlValues: {},
  isSubmitting: false,
  onResolve: null,

  showModal: (modal: PrismaModalPayload, onResolve) => {
    const initialValues = initializeControlValues(modal.controls)
    set({
      activeModal: modal,
      controlValues: initialValues,
      isSubmitting: false,
      onResolve: onResolve ?? null,
    })
  },

  clearModal: () => {
    set({
      activeModal: null,
      controlValues: {},
      isSubmitting: false,
      onResolve: null,
    })
  },

  setControlValue: (controlId: string, value: any) => {
    set((state) => ({
      controlValues: {
        ...state.controlValues,
        [controlId]: value,
      },
    }))
  },

  toggleChecklistItem: (controlId: string, itemId: string, checked?: boolean) => {
    const { activeModal, controlValues } = get()
    if (!activeModal) return

    const control = activeModal.controls?.find((c) => c.id === controlId && c.type === 'checklist')
    if (!control) return

    const itemDef = control.items?.find((i) => i.id === itemId)
    if (itemDef?.checkboxDisabled) return

    const currentChecklist = controlValues[controlId] as PrismaModalChecklistValue | undefined
    const existingItems = currentChecklist?.items || {}
    const prevChecked = existingItems[itemId]?.checked ?? (itemDef?.checked ?? true)
    const nextChecked = checked !== undefined ? checked : !prevChecked

    const nextItems: Record<string, PrismaModalChecklistItemState> = {
      ...existingItems,
      [itemId]: {
        ...existingItems[itemId],
        checked: nextChecked,
      },
    }

    set({
      controlValues: {
        ...controlValues,
        [controlId]: buildChecklistValueFromItems(control, nextItems),
      },
    })
  },

  setAllChecklistItems: (controlId: string, checked: boolean) => {
    const { activeModal, controlValues } = get()
    if (!activeModal) return

    const control = activeModal.controls?.find((c) => c.id === controlId && c.type === 'checklist')
    if (!control) return

    const currentChecklist = controlValues[controlId] as PrismaModalChecklistValue | undefined
    const existingItems = currentChecklist?.items || {}
    const nextItems: Record<string, PrismaModalChecklistItemState> = { ...existingItems }

    for (const item of control.items || []) {
      const prev = existingItems[item.id]
      nextItems[item.id] = {
        ...prev,
        checked: item.checkboxDisabled ? (prev?.checked ?? Boolean(item.checked)) : checked,
      }
    }

    set({
      controlValues: {
        ...controlValues,
        [controlId]: buildChecklistValueFromItems(control, nextItems),
      },
    })
  },

  setChecklistItemSelectValue: (controlId: string, itemId: string, selectValue: string) => {
    const { activeModal, controlValues } = get()
    if (!activeModal) return

    const control = activeModal.controls?.find((c) => c.id === controlId && c.type === 'checklist')
    if (!control) return

    const currentChecklist = controlValues[controlId] as PrismaModalChecklistValue | undefined
    const existingItems = currentChecklist?.items || {}
    const prev = existingItems[itemId] || { checked: true }

    const nextItems: Record<string, PrismaModalChecklistItemState> = {
      ...existingItems,
      [itemId]: {
        ...prev,
        selectValue,
      },
    }

    set({
      controlValues: {
        ...controlValues,
        [controlId]: buildChecklistValueFromItems(control, nextItems),
      },
    })
  },

  submitModal: (buttonId: string, buttonIndex: number, customValues?: Record<string, any>) => {
    const { activeModal, controlValues, isSubmitting, onResolve } = get()
    if (!activeModal || isSubmitting) return

    set({ isSubmitting: true })

    const finalValues = customValues
      ? { ...controlValues, ...customValues }
      : controlValues

    if (onResolve) {
      set({
        activeModal: null,
        controlValues: {},
        isSubmitting: false,
        onResolve: null,
      })
      onResolve(buttonId, buttonIndex, finalValues)
      return
    }

    sendMarasAction('modalResponse', {
      modalId: activeModal.modalId,
      buttonId,
      buttonIndex,
      values: finalValues,
    })

    // In standalone browser dev mode, clear the modal immediately after dispatching
    if (!isInGame()) {
      set({
        activeModal: null,
        controlValues: {},
        isSubmitting: false,
        onResolve: null,
      })
    }
  },
}))

export function confirmRemoveRelationship(
  actorAName: string,
  actorBName: string,
  onConfirm: () => void,
) {
  useModalStore.getState().showModal(
    {
      modalId: 'confirm_remove_relationship',
      title: 'Remove Relationship Record?',
      subtitle: `${actorAName} & ${actorBName}`,
      message: 'Are you sure you want to permanently remove this relationship record, its status history, and any associated affair records from M.A.R.A.S tracking?\n\nBoth NPCs will remain registered as Candidates.',
      size: 'sm',
      tone: 'danger',
      buttons: [
        { id: 'remove', label: 'Remove Record', variant: 'danger' },
        { id: 'cancel', label: 'Cancel', variant: 'secondary' },
      ],
    },
    (buttonId) => {
      if (buttonId === 'remove') {
        onConfirm()
      }
    },
  )
}

export function confirmRemoveChild(
  childId: string,
  childName: string,
  onConfirm: () => void,
) {
  useModalStore.getState().showModal(
    {
      modalId: 'confirm_remove_child',
      title: 'Remove Child Record?',
      subtitle: `${childName} (${childId})`,
      message: 'Are you sure you want to permanently remove this child record from M.A.R.A.S household and family tracking?\n\nThis will clear their guardian and lineage assignments in M.A.R.A.S.',
      size: 'sm',
      tone: 'danger',
      buttons: [
        { id: 'remove', label: 'Remove Child Record', variant: 'danger' },
        { id: 'cancel', label: 'Cancel', variant: 'secondary' },
      ],
    },
    (buttonId) => {
      if (buttonId === 'remove') {
        onConfirm()
      }
    },
  )
}


