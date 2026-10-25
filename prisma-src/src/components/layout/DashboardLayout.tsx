import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { Sidebar, mainNavTabs, subNavConfig } from './Sidebar'
import {
  Baby,
  Sparkles,
  X,
  Terminal,
  Eye,
  EyeOff,
  RefreshCw,
  Pause,
  Maximize2,
  Minimize2,
  Minus,
  Plus,
} from 'lucide-react'
import { AppTooltip } from '../shared/AppTooltip'

interface DashboardLayoutProps {
  children: React.ReactNode
  style?: React.CSSProperties
}

export const DashboardLayout: React.FC<DashboardLayoutProps> = ({ children, style }) => {
  const closeUi = useMarasStore((s) => s.closeUi)
  const refreshData = useMarasStore((s) => s.refreshData)
  const fleeChildrenFromCell = useMarasStore((s) => s.fleeChildrenFromCell)
  const hasChildren = useMarasStore((s) => s.children.size > 0)
  const statusMsg = useMarasStore((s) => s.statusMsg)
  const isInGame = useMarasStore((s) => s.isInGame)
  const isGamePaused = useMarasStore((s) => s.isGamePaused)
  const toggleGamePaused = useMarasStore((s) => s.toggleGamePaused)
  const logTestSignal = useMarasStore((s) => s.logTestSignal)
  const activeTab = useMarasStore((s) => s.activeTab)
  const activeSubSection = useMarasStore((s) => s.activeSubSection)
  const revealSecretsInUI = useMarasStore((s) => Boolean(s.settings.revealSecretsInUI))
  const uiFullScreen = useMarasStore((s) => Boolean(s.settings.uiFullScreen))
  const uiScale = useMarasStore((s) => s.settings.uiScale ?? 1)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const [isSpinningRefresh, setIsSpinningRefresh] = useState(false)

  const handleRefresh = () => {
    setIsSpinningRefresh(true)
    refreshData()
    setTimeout(() => setIsSpinningRefresh(false), 500)
  }

  const handleScaleStep = (delta: number) => {
    const next = Math.round(Math.min(1.5, Math.max(0.75, uiScale + delta)) * 100) / 100
    updateSetting('uiScale', next)
  }

  const currentTabObj = mainNavTabs.find((t) => t.id === activeTab)
  const currentSubObj = subNavConfig[activeTab]?.find((s) => s.id === activeSubSection)
  const ActiveTabIcon = currentTabObj?.icon || Sparkles

  return (
    <div
      className={`ui-container h-full w-full flex items-center justify-center ${
        uiFullScreen ? 'p-0' : 'p-[1rem]'
      } bg-overlay select-none font-ui bg-cover bg-center bg-no-repeat overflow-hidden`}
      style={style}
    >
      <div
        className={`prism-panel w-full h-full ${
          uiFullScreen
            ? 'max-w-none max-h-none rounded-none border-0'
            : 'max-w-[68rem] max-h-[44rem] rounded-lg border border-border-strong'
        } bg-window shadow-window flex flex-col overflow-hidden relative`}
      >
        {/* Header bar */}
        <header className="header flex items-center justify-between px-[1.25rem] py-[0.75rem] border-b border-border-divider bg-panel">
          <div className="flex items-center gap-[1rem]">
            <div className="flex flex-col">
              <h1 className="text-xl font-bold tracking-wide text-accent-bright flex items-center gap-[0.375rem] leading-tight">
                <Sparkles className="w-[0.875em] h-[0.875em] text-accent" /> M.A.R.A.S
              </h1>
              <span className="text-xs text-foreground-secondary leading-tight">
                Marriage & Relationship Alignment System
              </span>
            </div>

            <div className="flex items-center gap-[0.5rem]">
              <AppTooltip
                side="bottom"
                content={
                  revealSecretsInUI
                    ? 'All Secrets Revealed — Showing all hidden affairs and paternity secrets. Click to hide undiscovered secrets (Intrigue Mode).'
                    : 'Secrets Hidden (Intrigue Mode) — Undiscovered affairs and paternity secrets are masked as if no secrets exist. Click to reveal all secrets.'
                }
              >
                <button
                  type="button"
                  onClick={() => updateSetting('revealSecretsInUI', !revealSecretsInUI)}
                  className={`h-[2rem] px-[0.625rem] rounded-md border transition-all cursor-pointer flex items-center justify-center gap-[0.3125rem] text-xs font-semibold shadow-raised ${
                    revealSecretsInUI
                      ? 'text-accent-bright bg-accent/25 border-border-accent shadow-glow hover:bg-accent/35'
                      : 'text-foreground-primary bg-panel-raised border-border-strong hover:text-accent-bright hover:border-border-accent hover:bg-accent-soft'
                  }`}
                >
                  {revealSecretsInUI ? (
                    <Eye className="w-[1rem] h-[1rem] text-accent-bright shrink-0" />
                  ) : (
                    <EyeOff className="w-[1rem] h-[1rem] text-accent shrink-0" />
                  )}
                  <span>Secrets</span>
                </button>
              </AppTooltip>
              <AppTooltip
                side="bottom"
                content="Reload Data — Refresh all relationships, NPC profiles, children, household buffs, and settings from SKSE."
              >
                <button
                  type="button"
                  onClick={handleRefresh}
                  className="h-[2rem] px-[0.625rem] rounded-md border transition-all cursor-pointer flex items-center justify-center gap-[0.3125rem] text-xs font-semibold text-foreground-primary bg-panel-raised border-border-strong shadow-raised hover:text-accent-bright hover:border-border-accent hover:bg-accent-soft"
                >
                  <RefreshCw
                    className={`w-[1rem] h-[1rem] text-accent shrink-0 transition-transform duration-500 ${
                      isSpinningRefresh ? 'rotate-180 text-accent-bright' : ''
                    }`}
                  />
                  <span>Reload</span>
                </button>
              </AppTooltip>
              <AppTooltip
                side="bottom"
                content={
                  hasChildren
                    ? 'Make all children in your current cell run away until you leave the cell (they flee through the nearest door; children still stuck next to you after ~12 seconds are moved out of the way). Useful before an intimate scene.'
                    : 'No children to send away.'
                }
              >
                <span className="inline-flex">
                  <button
                    type="button"
                    onClick={fleeChildrenFromCell}
                    disabled={!hasChildren}
                    className="h-[2rem] px-[0.625rem] rounded-md border transition-all cursor-pointer flex items-center justify-center gap-[0.3125rem] text-xs font-semibold text-foreground-primary bg-panel-raised border-border-strong shadow-raised hover:text-accent-bright hover:border-border-accent hover:bg-accent-soft disabled:opacity-55 disabled:cursor-not-allowed disabled:hover:text-foreground-primary disabled:hover:border-border-strong disabled:hover:bg-panel-raised"
                  >
                    <Baby className="w-[1rem] h-[1rem] text-accent shrink-0" />
                    <span>Send Kids</span>
                  </button>
                </span>
              </AppTooltip>
            </div>
          </div>

          {/* Active View Breadcrumb Indicator */}
          {currentTabObj && (
            <div className="h-[2rem] flex items-center gap-[0.5rem] bg-panel-inset px-[0.75rem] rounded-md border border-border-subtle text-xs font-medium">
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
            {/* Accessibility Quick Font & Element Scale Controls */}
            <div className="h-[2rem] flex items-center bg-panel-raised border border-border-strong rounded-md p-[0.125rem] shadow-raised">
              <AppTooltip side="bottom" content="Decrease UI & Font Scale (-5%)">
                <button
                  type="button"
                  onClick={() => handleScaleStep(-0.05)}
                  disabled={uiScale <= 0.75}
                  className="h-full px-[0.4375rem] rounded-sm text-xs text-foreground-primary hover:text-accent-bright hover:bg-accent-soft disabled:opacity-40 disabled:pointer-events-none cursor-pointer transition-colors flex items-center"
                >
                  <Minus className="w-[0.95rem] h-[0.95rem] text-accent" />
                </button>
              </AppTooltip>
              <AppTooltip
                side="bottom"
                content="Accessibility UI & Font Scale — Click to reset to 100%"
              >
                <button
                  type="button"
                  onClick={() => updateSetting('uiScale', 1)}
                  className={`h-full px-[0.4375rem] rounded-sm text-xs font-semibold cursor-pointer transition-colors flex items-center ${
                    Math.round(uiScale * 100) !== 100
                      ? 'text-accent-bright bg-accent/20'
                      : 'text-foreground-primary hover:text-accent-bright'
                  }`}
                >
                  {Math.round(uiScale * 100)}%
                </button>
              </AppTooltip>
              <AppTooltip side="bottom" content="Increase UI & Font Scale (+5%)">
                <button
                  type="button"
                  onClick={() => handleScaleStep(0.05)}
                  disabled={uiScale >= 1.5}
                  className="h-full px-[0.4375rem] rounded-sm text-xs text-foreground-primary hover:text-accent-bright hover:bg-accent-soft disabled:opacity-40 disabled:pointer-events-none cursor-pointer transition-colors flex items-center"
                >
                  <Plus className="w-[0.95rem] h-[0.95rem] text-accent" />
                </button>
              </AppTooltip>
            </div>

            <AppTooltip
              side="bottom"
              content={
                isGamePaused
                  ? 'Game Paused — Skyrim world time and AI are paused while you browse. Click to unpause the game.'
                  : 'Pause Game — Click to pause Skyrim while the dashboard is open.'
              }
            >
              <button
                type="button"
                onClick={toggleGamePaused}
                className={`h-[2rem] px-[0.625rem] rounded-md border transition-all cursor-pointer flex items-center justify-center gap-[0.3125rem] text-xs font-semibold shadow-raised ${
                  isGamePaused
                    ? 'text-accent-bright bg-accent/25 border-border-accent shadow-glow hover:bg-accent/35'
                    : 'text-foreground-primary bg-panel-raised border-border-strong hover:text-accent-bright hover:border-border-accent hover:bg-accent-soft'
                }`}
              >
                <Pause
                  className={`w-[1rem] h-[1rem] shrink-0 ${
                    isGamePaused ? 'text-accent-bright' : 'text-accent'
                  }`}
                />
                <span>{isGamePaused ? 'Paused' : 'Pause'}</span>
              </button>
            </AppTooltip>

            <AppTooltip
              side="bottom"
              content={
                uiFullScreen
                  ? 'Windowed Mode — Restore centered dashboard window'
                  : 'Full Screen Mode — Expand dashboard to fill the screen'
              }
            >
              <button
                type="button"
                onClick={() => updateSetting('uiFullScreen', !uiFullScreen)}
                className={`w-[2rem] h-[2rem] rounded-md border transition-all cursor-pointer flex items-center justify-center shadow-raised ${
                  uiFullScreen
                    ? 'text-accent-bright bg-accent/25 border-border-accent shadow-glow hover:bg-accent/35'
                    : 'text-foreground-primary bg-panel-raised border-border-strong hover:text-accent-bright hover:border-border-accent hover:bg-accent-soft'
                }`}
              >
                {uiFullScreen ? (
                  <Minimize2 className="w-[1.05rem] h-[1.05rem] text-accent-bright" />
                ) : (
                  <Maximize2 className="w-[1.05rem] h-[1.05rem] text-accent" />
                )}
              </button>
            </AppTooltip>

            <AppTooltip content="Close UI (Esc)">
              <button
                type="button"
                onClick={closeUi}
                className="w-[2rem] h-[2rem] rounded-md border border-border-strong bg-panel-raised text-foreground-primary shadow-raised hover:text-status-danger hover:border-status-danger hover:bg-status-danger-soft transition-all cursor-pointer flex items-center justify-center"
              >
                <X className="w-[1.15rem] h-[1.15rem]" />
              </button>
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
