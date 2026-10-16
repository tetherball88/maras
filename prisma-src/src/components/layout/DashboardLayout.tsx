import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { Sidebar, mainNavTabs, subNavConfig } from './Sidebar'
import { Baby, Sparkles, X, Terminal } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { AppTooltip } from '../shared/AppTooltip'

interface DashboardLayoutProps {
  children: React.ReactNode
  style?: React.CSSProperties
}

export const DashboardLayout: React.FC<DashboardLayoutProps> = ({ children, style }) => {
  const closeUi = useMarasStore((s) => s.closeUi)
  const fleeChildrenFromCell = useMarasStore((s) => s.fleeChildrenFromCell)
  const hasChildren = useMarasStore((s) => s.children.size > 0)
  const statusMsg = useMarasStore((s) => s.statusMsg)
  const isInGame = useMarasStore((s) => s.isInGame)
  const logTestSignal = useMarasStore((s) => s.logTestSignal)
  const activeTab = useMarasStore((s) => s.activeTab)
  const activeSubSection = useMarasStore((s) => s.activeSubSection)

  const currentTabObj = mainNavTabs.find((t) => t.id === activeTab)
  const currentSubObj = subNavConfig[activeTab]?.find((s) => s.id === activeSubSection)
  const ActiveTabIcon = currentTabObj?.icon || Sparkles

  return (
    <div
      className="ui-container h-full w-full flex items-center justify-center p-[1rem] bg-overlay select-none font-ui bg-cover bg-center bg-no-repeat overflow-hidden"
      style={style}
    >
      <div className="prism-panel w-full max-w-[68rem] h-full max-h-[44rem] rounded-lg border border-border-strong bg-window shadow-window flex flex-col overflow-hidden relative">
        {/* Header bar */}
        <header className="header flex items-center justify-between px-[1.25rem] py-[0.75rem] border-b border-border-divider bg-panel">
          <div className="flex items-center gap-[1rem]">
            <div className="flex flex-col">
              <h1 className="text-xl font-bold tracking-wide text-accent-bright flex items-center gap-[0.375rem]">
                <Sparkles className="w-[0.875em] h-[0.875em] text-accent" /> M.A.R.A.S
              </h1>
              <span className="text-xs text-foreground-secondary">
                Marriage & Relationship Alignment System
              </span>
            </div>
          </div>

          {/* Active View Breadcrumb Indicator */}
          {currentTabObj && (
            <div className="flex items-center gap-[0.5rem] bg-panel-inset px-[0.75rem] py-[0.375rem] rounded-md border border-border-subtle text-xs font-medium">
              <ActiveTabIcon className="w-[1em] h-[1em] text-accent" />
              <span className="text-foreground-primary font-semibold">{currentTabObj.label}</span>
              {currentSubObj && (
                <>
                  <span className="text-foreground-muted">/</span>
                  <span className="text-foreground-secondary">{currentSubObj.label}</span>
                </>
              )}
            </div>
          )}

          <div className="flex items-center gap-[0.5rem]">
            <AppTooltip
              side="bottom"
              content={
                hasChildren
                  ? 'Make all children in your current cell run away until you leave the cell (they flee through the nearest door; children still stuck next to you after ~12 seconds are moved out of the way). Useful before an intimate scene.'
                  : 'No children to send away.'
              }
            >
              {/* Wrapper keeps the tooltip working while the button is disabled */}
              <span className="inline-flex">
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={fleeChildrenFromCell}
                  disabled={!hasChildren}
                  className="text-foreground-muted hover:text-accent-bright hover:bg-accent-soft rounded-full"
                >
                  <Baby className="w-[1.25em] h-[1.25em]" />
                </Button>
              </span>
            </AppTooltip>

            <AppTooltip content="Close UI (Esc)">
              <Button
                variant="ghost"
                size="icon"
                onClick={closeUi}
                className="text-foreground-muted hover:text-accent-bright hover:bg-accent-soft rounded-full"
              >
                <X className="w-[1.25em] h-[1.25em]" />
              </Button>
            </AppTooltip>
          </div>
        </header>

        {/* Main Body */}
        <div className="flex-1 flex overflow-hidden">
          <Sidebar />
          <main className="flex-1 bg-window relative h-full min-h-0 overflow-hidden p-[1.25rem]">
            {children}
          </main>
        </div>

        {/* Footer Bar */}
        <footer className="px-[1.25rem] py-[0.5rem] bg-panel border-t border-border-divider flex items-center justify-between text-xs text-foreground-muted">
          <div className="flex items-center gap-[0.5rem]">
            <span className={`w-[0.5rem] h-[0.5rem] rounded-full inline-block ${isInGame ? 'bg-status-success' : 'bg-accent'}`}></span>
            <span>{statusMsg}</span>
            <span className="text-border-divider">|</span>
            <span className={isInGame ? 'text-status-success font-semibold' : 'text-accent-bright'}>
              {isInGame ? 'Live SKSE Mode' : 'Standalone Mock Mode'}
            </span>
          </div>
          <div className="flex items-center gap-[0.75rem]">
            <button
              onClick={logTestSignal}
              className="flex items-center gap-[0.25rem] hover:text-accent cursor-pointer transition-colors"
            >
              <Terminal className="w-[1em] h-[1em]" />
              <span>Log Test Signal</span>
            </button>
            <span>v1.0.0</span>
          </div>
        </footer>
      </div>
    </div>
  )
}
