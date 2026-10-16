import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import {
  LayoutDashboard,
  Activity,
  Users,
  Heart,
  Network,
  Sparkles,
  Sliders,
  Bot,
  ShieldAlert,
  Wrench,
  PanelLeftClose,
  PanelLeftOpen,
  HeartHandshake,
  GitFork,
  Settings,
  Baby,
  Home,
  Zap,
  CheckSquare,
  Dna,
  Clapperboard,
} from 'lucide-react'
import { AppTooltip } from '../shared/AppTooltip'

export interface MainTabItem {
  id: string
  label: string
  icon: React.ElementType
}

export interface SubItem {
  id: string
  label: string
  icon: React.ElementType
}

export const mainNavTabs: MainTabItem[] = [
  { id: 'overview', label: 'Overview', icon: LayoutDashboard },
  { id: 'household', label: 'My Household', icon: Home },
  { id: 'matchmaking', label: 'Matchmaking', icon: HeartHandshake },
  { id: 'settings', label: 'Settings', icon: Settings },
]

export const subNavConfig: Record<string, SubItem[]> = {
  overview: [
    { id: 'dashboard', label: 'Dashboard', icon: LayoutDashboard },
    { id: 'activity', label: 'Activity Feed', icon: Activity },
    { id: 'browse', label: 'Browse NPCs', icon: Network },
    { id: 'allPairs', label: 'All Pairs', icon: Sparkles },
  ],
  household: [
    { id: 'all', label: 'Partners', icon: Users },
    { id: 'children', label: 'Children', icon: Baby },
    { id: 'buffs', label: 'Household Buffs', icon: Zap },
  ],
  matchmaking: [
    { id: 'manualMatch', label: 'Manual Match', icon: Sparkles },
    { id: 'llmMatch', label: 'LLM Matchmaking', icon: Bot },
    { id: 'adoption', label: 'Child Adoption', icon: Baby },
  ],
  settings: [
    { id: 'general', label: 'General', icon: Sliders },
    { id: 'confirmations', label: 'Confirmations', icon: CheckSquare },
    { id: 'matchmaking', label: 'Matchmaking', icon: HeartHandshake },
    { id: 'scenes', label: 'NPC Scenes', icon: Clapperboard },
    { id: 'children', label: 'Children & Pets', icon: Baby },
    { id: 'fertility', label: 'Fertility', icon: Dna },
    { id: 'aiDialogue', label: 'AI Dialogues', icon: Bot },
    { id: 'skyrimNet', label: 'SkyrimNet', icon: ShieldAlert },
    { id: 'cheats', label: 'Cheats & Debug', icon: Wrench },
  ],
}

export const Sidebar: React.FC = () => {
  const activeTab = useMarasStore((s) => s.activeTab)
  const setActiveTab = useMarasStore((s) => s.setActiveTab)
  const activeSubSection = useMarasStore((s) => s.activeSubSection)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)
  const settings = useMarasStore((s) => s.settings)

  const [isCollapsed, setIsCollapsed] = useState(false)

  return (
    <aside
      className={`flex-shrink-0 bg-panel border-r border-border-divider flex flex-col justify-between transition-all duration-300 ease-in-out ${isCollapsed ? 'w-[3.5rem] p-[0.375rem]' : 'w-[13.5rem] p-[0.75rem]'
        }`}
    >
      <div className="flex flex-col gap-[0.375rem]">
        {/* Drawer Header & Collapse Toggle */}
        <div className="flex items-center justify-between px-[0.25rem] mb-[0.25rem]">
          {!isCollapsed && (
            <span className="text-xs uppercase tracking-wider font-semibold text-foreground-muted">
              Navigation
            </span>
          )}
          <AppTooltip content={isCollapsed ? 'Expand Sidebar' : 'Collapse Sidebar'} side="right">
            <button
              onClick={() => setIsCollapsed(!isCollapsed)}
              className="p-[0.25rem] rounded text-foreground-muted hover:text-accent-bright hover:bg-accent-soft cursor-pointer transition-colors ml-auto"
            >
              {isCollapsed ? (
                <PanelLeftOpen className="w-[1.25em] h-[1.25em]" />
              ) : (
                <PanelLeftClose className="w-[1.25em] h-[1.25em]" />
              )}
            </button>
          </AppTooltip>
        </div>

        {/* Main Nav Tabs */}
        {mainNavTabs.map((tab) => {
          const TabIcon = tab.icon
          const isTabActive = activeTab === tab.id
          const rawSubItems = subNavConfig[tab.id] || []
          const subItems = rawSubItems.filter((sub) => {
            if (sub.id === 'llmMatch' && !settings.hasSkyrimNet) return false
            return true
          })
          const tabButton = (
            <button
              onClick={() => setActiveTab(tab.id)}
              className={`flex items-center gap-[0.625rem] rounded-md text-sm font-medium transition-all duration-150 cursor-pointer ${isCollapsed ? 'justify-center p-[0.625rem]' : 'px-[0.75rem] py-[0.5rem]'
                } ${isTabActive
                  ? 'bg-accent-soft text-accent-bright font-semibold border-l-2 border-accent shadow-sm'
                  : 'text-foreground-secondary hover:text-foreground-primary hover:bg-interactive-hover'
                }`}
            >
              <TabIcon className={`w-[1.125em] h-[1.125em] flex-shrink-0 ${isTabActive ? 'text-accent' : 'text-foreground-muted'}`} />
              {!isCollapsed && <span className="truncate flex-1 text-left">{tab.label}</span>}
            </button>
          )

          return (
            <div key={tab.id} className="flex flex-col gap-[0.125rem]">
              {isCollapsed ? (
                <AppTooltip content={tab.label} side="right">
                  {tabButton}
                </AppTooltip>
              ) : (
                tabButton
              )}

              {/* Sub-Sections for Active Main Tab */}
              {!isCollapsed && isTabActive && subItems.length > 0 && (
                <div className="ml-[1.25rem] pl-[0.5rem] border-l border-border-divider flex flex-col gap-[0.125rem] my-[0.125rem]">
                  {subItems.map((sub) => {
                    const SubIcon = sub.icon
                    const isSubActive = activeSubSection === sub.id
                    return (
                      <button
                        key={sub.id}
                        onClick={() => setActiveSubSection(sub.id)}
                        className={`flex items-center gap-[0.5rem] px-[0.625rem] py-[0.375rem] rounded-md text-xs transition-all duration-150 cursor-pointer ${isSubActive
                          ? 'bg-panel-inset text-accent-bright font-semibold text-accent'
                          : 'text-foreground-muted hover:text-foreground-primary hover:bg-interactive-hover'
                          }`}
                      >
                        <SubIcon className={`w-[0.875em] h-[0.875em] flex-shrink-0 ${isSubActive ? 'text-accent' : 'text-foreground-muted'}`} />
                        <span className="truncate">{sub.label}</span>
                      </button>
                    )
                  })}
                </div>
              )}
            </div>
          )
        })}
      </div>
    </aside>
  )
}
