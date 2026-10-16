import React, { CSSProperties, useEffect } from 'react'
import { useMarasStore } from '@/store/useMarasStore'
import { sendMarasAction } from '@/store/prismaBridge'
import { DashboardLayout } from '@/components/layout/DashboardLayout'
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
  const setIsOpen = useMarasStore((s) => s.setIsOpen)

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

    // Make sure UI state is open when mounted inside PrismaUI
    setIsOpen(true)
    window.log?.('MARAS Prisma UI View mounted successfully')

    // Automatically request live overview data on mount to avoid missing DOM ready events
    if (inGame) {
      sendMarasAction('fetchOverview')
    }
  }, [setStatusMsg, setIsOpen, inGame])

  if (!isOpen) return null

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

  return <DashboardLayout style={style}>{renderContent()}</DashboardLayout>
}

