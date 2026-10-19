import React from 'react'
import { PrismaModalPayload } from '@/types/modal'

export interface CustomModalTemplateProps {
  modal: PrismaModalPayload
  values: Record<string, any>
  onSubmit: (buttonId: string, buttonIndex: number, customValues?: Record<string, any>) => void
}

const modalTemplateRegistry = new Map<string, React.ComponentType<CustomModalTemplateProps>>()

export function registerModalTemplate(
  modalType: string,
  component: React.ComponentType<CustomModalTemplateProps>,
) {
  modalTemplateRegistry.set(modalType, component)
}

export function getModalTemplate(
  modalType?: string,
): React.ComponentType<CustomModalTemplateProps> | undefined {
  if (!modalType || modalType === 'generic') {
    return undefined
  }
  return modalTemplateRegistry.get(modalType)
}

