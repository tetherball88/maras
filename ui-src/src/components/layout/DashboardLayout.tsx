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
import { useTranslation } from '../../i18n'

interface DashboardLayoutProps {
  children: React.ReactNode
  style?: React.CSSProperties
}

export const DashboardLayout: React.FC<DashboardLayoutProps> = ({ children, style }) => {
  const { t } = useTranslation()
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

  const currentTabObj = mainNavTabs.find((tab) => tab.id === activeTab)
  const currentSubObj = subNavConfig[activeTab]?.find((s) => s.id === activeSubSection)
  const ActiveTabIcon = currentTabObj?.icon || Sparkles

  const resolvedStatusMsg =
    statusMsg === 'M.A.R.A.S Core Active'
      ? t('layout.footer.statusCoreActive')
      : statusMsg === 'UI Dev Mode'
        ? t('layout.footer.statusDevMode')
        : statusMsg || t('layout.footer.statusReady')

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
                <Sparkles className="w-[0.875em] h-[0.875em] text-accent" /> {t('layout.header.brandTitle')}
              </h1>
              <span className="text-xs text-foreground-secondary leading-tight">
                {t('layout.header.brandSubtitle')}
              </span>
            </div>

            <div className="flex items-center gap-[0.5rem]">
              <AppTooltip
                side="bottom"
                content={
                  revealSecretsInUI
                    ? t('layout.header.secretsRevealedTooltip')
                    : t('layout.header.secretsHiddenTooltip')
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
                  <span>{t('layout.header.secretsButton')}</span>
                </button>
              </AppTooltip>
              <AppTooltip
                side="bottom"
                content={t('layout.header.reloadTooltip')}
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
                  <span>{t('layout.header.reloadButton')}</span>
                </button>
              </AppTooltip>
              <AppTooltip
                side="bottom"
                content={
                  hasChildren
                    ? t('layout.header.sendKidsTooltip')
                    : t('layout.header.sendKidsEmptyTooltip')
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
                    <span>{t('layout.header.sendKidsButton')}</span>
                  </button>
                </span>
              </AppTooltip>
            </div>
          </div>

          {/* Active View Breadcrumb Indicator */}
          {currentTabObj && (
            <div className="h-[2rem] flex items-center gap-[0.5rem] bg-panel-inset px-[0.75rem] rounded-md border border-border-subtle text-xs font-medium">
              <ActiveTabIcon className="w-[1em] h-[1em] text-accent" />
              <span className="text-foreground-primary font-semibold">
                {t(currentTabObj.labelKey, undefined, currentTabObj.label)}
              </span>
              {currentSubObj && (
                <>
                  <span className="text-foreground-muted">/</span>
                  <span className="text-foreground-secondary">
                    {t(currentSubObj.labelKey, undefined, currentSubObj.label)}
                  </span>
                </>
              )}
            </div>
          )}

          <div className="flex items-center gap-[0.5rem]">
            {/* Accessibility Quick Font & Element Scale Controls */}
            <div className="h-[2rem] flex items-center bg-panel-raised border border-border-strong rounded-md p-[0.125rem] shadow-raised">
              <AppTooltip side="bottom" content={t('layout.header.scaleDecreaseTooltip')}>
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
                content={t('layout.header.scaleResetTooltip')}
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
              <AppTooltip side="bottom" content={t('layout.header.scaleIncreaseTooltip')}>
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
                  ? t('layout.header.gamePausedTooltip')
                  : t('layout.header.pauseGameTooltip')
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
                <span>{isGamePaused ? t('layout.header.pausedButton') : t('layout.header.pauseButton')}</span>
              </button>
            </AppTooltip>

            <AppTooltip
              side="bottom"
              content={
                uiFullScreen
                  ? t('layout.header.windowedModeTooltip')
                  : t('layout.header.fullScreenModeTooltip')
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

            <AppTooltip content={t('layout.header.closeTooltip')}>
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
            <span>{resolvedStatusMsg}</span>
            <span className="text-border-divider">|</span>
            <span className={isInGame ? 'text-status-success font-semibold' : 'text-accent-bright'}>
              {isInGame ? t('layout.footer.liveSkseMode') : t('layout.footer.standaloneMockMode')}
            </span>
          </div>
          <div className="flex items-center gap-[0.75rem]">
            <button
              onClick={logTestSignal}
              className="flex items-center gap-[0.25rem] hover:text-accent cursor-pointer transition-colors"
            >
              <Terminal className="w-[1em] h-[1em]" />
              <span>{t('layout.footer.logTestSignal')}</span>
            </button>
            <span>{t('layout.footer.version', { version: '1.0.0' })}</span>
          </div>
        </footer>
      </div>
    </div>
  )
}
