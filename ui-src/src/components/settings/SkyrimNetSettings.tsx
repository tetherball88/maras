import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SectionCard } from '../shared/SectionCard'
import { CheckCircle, AlertTriangle, Target, Brain } from 'lucide-react'

export const SkyrimNetSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  const isGatedDisabled = !settings.hasSkyrimNet

  const toggleSimSlot = (
    <label className="flex items-center gap-[0.375rem] text-xs text-foreground-secondary cursor-pointer">
      <span>{t('settings.skyrimNet.simulateToggleLabel')}</span>
      <ToggleSwitch
        checked={Boolean(settings.simulateNoSkyrimNet)}
        onChange={(val) => updateSetting('simulateNoSkyrimNet', val)}
      />
    </label>
  )

  return (
    <div className="space-y-[1rem]">
      {isGatedDisabled ? (
        <div className="bg-status-warning-soft border border-status-warning rounded-md p-[0.875rem] text-xs text-foreground-primary flex items-start gap-[0.5rem]">
          <AlertTriangle className="w-[1.25em] h-[1.25em] text-status-warning flex-shrink-0 mt-[0.125rem]" />
          <div className="flex-1">
            <div className="font-semibold text-status-warning mb-[0.125rem]">
              {settings.simulateNoSkyrimNet
                ? t('settings.skyrimNet.disabledSimTitle')
                : t('settings.skyrimNet.notDetectedTitle')}
            </div>
            <div>
              {settings.simulateNoSkyrimNet
                ? t('settings.skyrimNet.disabledSimDesc')
                : t('settings.skyrimNet.notDetectedDesc')}
            </div>
          </div>
          {settings.simulateNoSkyrimNet && (
            <button
              type="button"
              onClick={() => updateSetting('simulateNoSkyrimNet', false)}
              className="px-2 py-1 text-xs font-medium rounded border border-status-warning/40 text-status-warning hover:bg-status-warning/20 transition-all cursor-pointer whitespace-nowrap"
            >
              {t('settings.skyrimNet.disableSimBtn')}
            </button>
          )}
        </div>
      ) : (
        <div className="bg-status-success-soft border border-status-success rounded-md p-[0.625rem] text-xs text-foreground-primary flex items-center gap-[0.375rem]">
          <CheckCircle className="w-[1.125em] h-[1.125em] text-status-success" />
          <span>{t('settings.skyrimNet.activeBanner')}</span>
        </div>
      )}

      <SectionCard
        title={t('settings.skyrimNet.actionDetectionTitle')}
        icon={Target}
        actionSlot={toggleSimSlot}
      >
        <SettingRow
          label={t('settings.skyrimNet.actionRadiusLabel')}
          description={t('settings.skyrimNet.actionRadiusDesc')}
          disabled={isGatedDisabled}
        >
          <input
            type="number"
            min={100}
            max={20000}
            step={100}
            value={settings.skyrimnetActionRadius ?? 3000}
            onChange={(e) => {
              const val = parseFloat(e.target.value)
              if (!isNaN(val) && val > 0) {
                updateSetting('skyrimnetActionRadius', val)
              }
            }}
            disabled={isGatedDisabled}
            className="w-28 px-2 py-1 bg-surface-secondary border border-border-light rounded text-sm text-foreground-primary focus:outline-none focus:border-primary disabled:opacity-50 text-right"
          />
        </SettingRow>
      </SectionCard>

      <SectionCard title={t('settings.skyrimNet.evalMemoryTitle')} icon={Brain}>
        <SettingRow
          label={t('settings.skyrimNet.useDecisionsLabel')}
          description={t('settings.skyrimNet.useDecisionsDesc')}
          disabled={isGatedDisabled}
        >
          <ToggleSwitch
            checked={settings.skyrimnetUseDecisions ?? true}
            onChange={(val) => updateSetting('skyrimnetUseDecisions', val)}
            disabled={isGatedDisabled}
          />
        </SettingRow>
        <SettingRow
          label={t('settings.skyrimNet.llmMemoriesLabel')}
          description={t('settings.skyrimNet.llmMemoriesDesc')}
          disabled={isGatedDisabled}
        >
          <ToggleSwitch
            checked={settings.skyrimnetUseLLMMemories ?? true}
            onChange={(val) => updateSetting('skyrimnetUseLLMMemories', val)}
            disabled={isGatedDisabled}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
