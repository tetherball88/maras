import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { LayoutDashboard, Home, HeartHandshake, Settings } from 'lucide-react'
import { useTranslation } from '../../i18n'

export const tabs = [
  { id: 'overview', label: 'Overview', labelKey: 'layout.nav.overview', icon: LayoutDashboard },
  { id: 'household', label: 'My Household', labelKey: 'layout.nav.household', icon: Home },
  { id: 'matchmaking', label: 'Matchmaking', labelKey: 'layout.nav.matchmaking', icon: HeartHandshake },
  { id: 'settings', label: 'Settings', labelKey: 'layout.nav.settings', icon: Settings },
]

export const TopTabBar: React.FC = () => {
  const { t } = useTranslation()
  const activeTab = useMarasStore((s) => s.activeTab)
  const setActiveTab = useMarasStore((s) => s.setActiveTab)

  return (
    <nav className="flex items-center gap-[0.25rem] bg-panel-inset p-[0.25rem] rounded-md border border-border-subtle">
      {tabs.map((tab) => {
        const Icon = tab.icon
        const isActive = activeTab === tab.id
        return (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            className={`flex items-center gap-[0.5rem] px-[0.875rem] py-[0.5rem] rounded-sm text-sm font-medium transition-all duration-150 cursor-pointer ${isActive
              ? 'bg-accent-soft text-accent-bright border-b-2 border-accent shadow-sm'
              : 'text-foreground-secondary hover:text-foreground-primary hover:bg-interactive-hover'
              }`}
          >
            <Icon className={`w-[1.125em] h-[1.125em] ${isActive ? 'text-accent' : 'text-foreground-muted'}`} />
            <span>{t(tab.labelKey, undefined, tab.label)}</span>
          </button>
        )
      })}
    </nav>
  )
}
