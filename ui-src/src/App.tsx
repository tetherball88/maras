import React, { CSSProperties, useEffect } from 'react'
import { useMarasStore } from '@/store/useMarasStore'
import { useModalStore } from '@/store/useModalStore'
import { sendMarasAction } from '@/store/uiBridge'
import { DashboardLayout } from '@/components/layout/DashboardLayout'
import { ModalHost } from '@/components/modals/ModalHost'
import { OverviewPage } from '@/components/pages/OverviewPage'
import { MyHouseholdPage } from '@/components/pages/MyHouseholdPage'
import { MatchmakingPage } from '@/components/pages/MatchmakingPage'
import { SettingsPage } from '@/components/pages/SettingsPage'
import { NpcDetailShell } from '@/components/npcDetail/NpcDetailShell'
import {
  convertKeyboardEventToScanCode,
  isEditableTarget,
  isEscapeKey,
} from '@/utils/keys'
import { isInGame } from './lib/utils'

declare global {
  interface Window {
    closeMARAS?: () => void
    log?: (message: string) => void
    receiveMARASStatus?: (statusJson: string | object) => void
  }
}

export const App: React.FC = () => {
  const isOpen = useMarasStore((s) => s.isOpen)
  const closeUi = useMarasStore((s) => s.closeUi)
  const settings = useMarasStore((s) => s.settings)
  const activeTab = useMarasStore((s) => s.activeTab)
  const npcDetailId = useMarasStore((s) => s.npcDetailId)
  const setStatusMsg = useMarasStore((s) => s.setStatusMsg)
  const uiScale = settings.uiScale ?? 1
  const hasActiveModal = useModalStore((s) => s.activeModal !== null)

  useEffect(() => {
    if (!isOpen || hasActiveModal) return

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.defaultPrevented) return

      if (isEscapeKey(e)) {
        if (document.querySelector('[role="listbox"], [role="menu"]')) {
          return
        }
        e.preventDefault()
        closeUi()
        return
      }

      if (!settings.toggleUIKey || settings.toggleUIKey === 0) return
      if (isEditableTarget(e.target)) return

      const scanCode = convertKeyboardEventToScanCode(e)
      if (scanCode !== settings.toggleUIKey) return

      if (
        e.ctrlKey !== Boolean(settings.toggleUIModCtrl) ||
        e.shiftKey !== Boolean(settings.toggleUIModShift) ||
        e.altKey !== Boolean(settings.toggleUIModAlt)
      ) {
        return
      }

      e.preventDefault()
      closeUi()
    }

    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [
    isOpen,
    hasActiveModal,
    closeUi,
    settings.toggleUIKey,
    settings.toggleUIModCtrl,
    settings.toggleUIModShift,
    settings.toggleUIModAlt,
  ])

  useEffect(() => {
    const clampedScale = Math.min(1.5, Math.max(0.75, Number.isFinite(uiScale) ? uiScale : 1))
    document.documentElement.style.setProperty('--ui-scale', String(clampedScale))
  }, [uiScale])

  const inGame = isInGame()
  const style: CSSProperties = !inGame
    ? {
        backgroundImage: 'url("./assets/background.jpg")',
        backgroundSize: 'cover',
        backgroundPosition: 'center',
        backgroundRepeat: 'no-repeat',
      }
    : {}

  useEffect(() => {
    window.receiveMARASStatus = (statusJson: string | object) => {
      try {
        if (typeof statusJson === 'object' && statusJson !== null) {
          const data = statusJson as any
          if (data.status) setStatusMsg(data.status)
        } else {
          const data = JSON.parse(statusJson as string)
          if (data.status) setStatusMsg(data.status)
        }
      } catch {
        setStatusMsg(String(statusJson))
      }
    }

    window.log?.('MARAS UI View mounted successfully')

    // Notify C++ that the React UI tree has mounted so it can push either the Dashboard or active Modal
    if (inGame) {
      sendMarasAction('uiReady')
    }
  }, [setStatusMsg, inGame])

  if (!isOpen && !hasActiveModal) return null

  const renderContent = () => {
    if (npcDetailId) {
      return <NpcDetailShell />
    }

    switch (activeTab) {
      case 'overview':
        return <OverviewPage />
      case 'household':
      case 'partners':
        return <MyHouseholdPage />
      case 'matchmaking':
        return <MatchmakingPage />
      case 'settings':
        return <SettingsPage />
      default:
        return <OverviewPage />
    }
  }

  return (
    <>
      {isOpen && <DashboardLayout style={style}>{renderContent()}</DashboardLayout>}
      {hasActiveModal && <ModalHost style={!isOpen ? style : undefined} />}
    </>
  )
}


