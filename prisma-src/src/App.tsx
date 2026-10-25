import React, { CSSProperties, useEffect } from 'react'
import { useMarasStore } from '@/store/useMarasStore'
import { useModalStore } from '@/store/useModalStore'
import { sendMarasAction } from '@/store/prismaBridge'
import { DashboardLayout } from '@/components/layout/DashboardLayout'
import { PrismaModalHost } from '@/components/modals/PrismaModalHost'
import { OverviewPage } from '@/components/pages/OverviewPage'
import { MyHouseholdPage } from '@/components/pages/MyHouseholdPage'
import { MatchmakingPage } from '@/components/pages/MatchmakingPage'
import { SettingsPage } from '@/components/pages/SettingsPage'
import { NpcDetailShell } from '@/components/npcDetail/NpcDetailShell'
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
  const activeTab = useMarasStore((s) => s.activeTab)
  const npcDetailId = useMarasStore((s) => s.npcDetailId)
  const setStatusMsg = useMarasStore((s) => s.setStatusMsg)
  const uiScale = useMarasStore((s) => s.settings.uiScale ?? 1)
  const hasActiveModal = useModalStore((s) => s.activeModal !== null)

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

    window.log?.('MARAS Prisma UI View mounted successfully')

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
      {hasActiveModal && <PrismaModalHost style={!isOpen ? style : undefined} />}
    </>
  )
}


