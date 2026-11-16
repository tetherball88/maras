import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { AppTooltip } from '../shared/AppTooltip'
import { Eye, Dices, Bot } from 'lucide-react'

export const FertilitySettings: React.FC = () => {
  const { t, tp } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const cleanNum = (val: number | undefined, fallback: number) =>
    Number((val ?? fallback).toFixed(2))

  const unitPercent = t('settings.fertility.unitPercent')
  const unitPercentPerDay = t('settings.fertility.unitPercentPerDay')

  return (
    <div className="space-y-[1rem]">
      <SectionCard title={t('settings.fertility.formulasTitle')} icon={Eye}>
        {/* Subtle Physical Traits Formula */}
        <div className="p-[0.75rem] bg-panel-inset/80 border border-border-subtle rounded-lg space-y-[0.625rem]">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-foreground-primary">
              {t('settings.fertility.subtleFormulaTitle')}
            </span>
            <span className="text-[0.6875rem] font-mono text-accent-bright bg-accent/10 px-[0.375rem] py-[0.125rem] rounded border border-accent/20">
              {t('settings.fertility.formulaBadge')}
            </span>
          </div>

          <div className="flex flex-wrap items-center gap-[0.5rem] text-xs font-mono">
            {/* Base */}
            <AppTooltip content={t('settings.fertility.subtleBaseTooltip')}>
              <div className="flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus-within:border-border-accent">
                <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">
                  {t('settings.fertility.formulaBaseLabel')}
                </span>
                <input
                  type="number"
                  min={0}
                  max={100}
                  step={1}
                  value={cleanNum(settings.paternityBaseChance, 2)}
                  onChange={(e) =>
                    updateSetting(
                      'paternityBaseChance' as any,
                      Math.max(0, Math.min(100, Number(e.target.value)))
                    )
                  }
                  className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none"
                />
                <span className="text-foreground-muted">{unitPercent}</span>
              </div>
            </AppTooltip>

            <span className="text-foreground-muted font-bold text-sm">+</span>

            {/* (Days x Daily Growth) */}
            <div className="flex items-center gap-[0.375rem] bg-panel-raised/50 border border-border-subtle/60 rounded px-[0.5rem] py-[0.25rem]">
              <span className="text-foreground-muted">(</span>
              <span className="text-accent text-[0.6875rem] font-sans font-medium">
                {t('settings.fertility.formulaDaysLabel')}
              </span>
              <span className="text-foreground-muted">×</span>
              <AppTooltip content={t('settings.fertility.subtleDailyTooltip')}>
                <div className="flex items-center gap-[0.125rem] bg-panel-raised border border-border-subtle rounded px-[0.375rem] py-[0.125rem] focus-within:border-border-accent">
                  <input
                    type="number"
                    min={0}
                    max={20}
                    step={0.5}
                    value={cleanNum(settings.paternityDailyGrowth, 2.0)}
                    onChange={(e) =>
                      updateSetting(
                        'paternityDailyGrowth' as any,
                        Math.max(0, Math.min(20, Number(e.target.value)))
                      )
                    }
                    className="w-12 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none"
                  />
                  <span className="text-foreground-muted text-[0.6875rem]">
                    {unitPercentPerDay}
                  </span>
                </div>
              </AppTooltip>
              <span className="text-foreground-muted">)</span>
            </div>

            <span className="text-foreground-muted font-semibold text-xs">
              {t('settings.fertility.formulaMaxCapPrefix')}
            </span>

            {/* Max Cap */}
            <AppTooltip content={t('settings.fertility.subtleMaxTooltip')}>
              <div className="flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus-within:border-border-accent">
                <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">
                  {t('settings.fertility.formulaMaxLabel')}
                </span>
                <input
                  type="number"
                  min={0}
                  max={100}
                  step={1}
                  value={cleanNum(settings.paternityMaxChance, 60)}
                  onChange={(e) =>
                    updateSetting(
                      'paternityMaxChance' as any,
                      Math.max(0, Math.min(100, Number(e.target.value)))
                    )
                  }
                  className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none"
                />
                <span className="text-foreground-muted">{unitPercent}</span>
              </div>
            </AppTooltip>
          </div>

          <p className="text-[0.6875rem] text-foreground-muted">
            {t('settings.fertility.subtleFormulaHelp')}
          </p>
        </div>

        {/* Phenotype Mismatch Birth Evaluation Chance */}
        <SettingRow
          label={t('settings.fertility.phenotypeBirthLabel')}
          description={t('settings.fertility.phenotypeBirthDesc')}
        >
          <SliderInput
            value={settings.paternityPhenotypeBirthChance ?? 100}
            min={0}
            max={100}
            step={1}
            unit={unitPercent}
            onChange={(val) => updateSetting('paternityPhenotypeBirthChance' as any, val)}
          />
        </SettingRow>

        {/* Phenotype Mismatch Formula */}
        {(() => {
          const isBirth100 = (settings.paternityPhenotypeBirthChance ?? 100) >= 100
          const disabledTooltip = t('settings.fertility.phenotypeDisabledTooltip')
          return (
            <div
              className={`p-[0.75rem] bg-panel-inset/80 border border-border-subtle rounded-lg space-y-[0.625rem] transition-opacity ${
                isBirth100 ? 'opacity-60' : ''
              }`}
            >
              <div className="flex items-center justify-between">
                <span className="text-xs font-semibold text-foreground-primary">
                  {t('settings.fertility.phenotypeFormulaTitle')}
                </span>
                {isBirth100 ? (
                  <span
                    className="text-[0.6875rem] font-mono text-amber-400 bg-amber-500/10 px-[0.375rem] py-[0.125rem] rounded border border-amber-500/20"
                    title={t('settings.fertility.phenotypeInactiveBadgeTooltip')}
                  >
                    {t('settings.fertility.phenotypeInactiveBadge')}
                  </span>
                ) : (
                  <span className="text-[0.6875rem] font-mono text-purple-400 bg-purple-500/10 px-[0.375rem] py-[0.125rem] rounded border border-purple-500/20">
                    {t('settings.fertility.formulaBadge')}
                  </span>
                )}
              </div>

              <div className="flex flex-wrap items-center gap-[0.5rem] text-xs font-mono">
                {/* Base */}
                <AppTooltip
                  content={
                    isBirth100 ? disabledTooltip : t('settings.fertility.phenotypeBaseTooltip')
                  }
                >
                  <div
                    className={`flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] ${
                      isBirth100 ? 'cursor-not-allowed opacity-50' : 'focus-within:border-border-accent'
                    }`}
                  >
                    <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">
                      {t('settings.fertility.formulaBaseLabel')}
                    </span>
                    <input
                      type="number"
                      disabled={isBirth100}
                      min={0}
                      max={100}
                      step={1}
                      value={cleanNum(settings.paternityPhenotypeBaseChance, 15)}
                      onChange={(e) =>
                        updateSetting(
                          'paternityPhenotypeBaseChance' as any,
                          Math.max(0, Math.min(100, Number(e.target.value)))
                        )
                      }
                      className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none disabled:cursor-not-allowed"
                    />
                    <span className="text-foreground-muted">{unitPercent}</span>
                  </div>
                </AppTooltip>

                <span className="text-foreground-muted font-bold text-sm">+</span>

                {/* (Days x Daily Growth) */}
                <div
                  className={`flex items-center gap-[0.375rem] bg-panel-raised/50 border border-border-subtle/60 rounded px-[0.5rem] py-[0.25rem] ${
                    isBirth100 ? 'opacity-50' : ''
                  }`}
                >
                  <span className="text-foreground-muted">(</span>
                  <span className="text-accent text-[0.6875rem] font-sans font-medium">
                    {t('settings.fertility.formulaDaysLabel')}
                  </span>
                  <span className="text-foreground-muted">×</span>
                  <AppTooltip
                    content={
                      isBirth100 ? disabledTooltip : t('settings.fertility.phenotypeDailyTooltip')
                    }
                  >
                    <div
                      className={`flex items-center gap-[0.125rem] bg-panel-raised border border-border-subtle rounded px-[0.375rem] py-[0.125rem] ${
                        isBirth100 ? 'cursor-not-allowed' : 'focus-within:border-border-accent'
                      }`}
                    >
                      <input
                        type="number"
                        disabled={isBirth100}
                        min={0}
                        max={20}
                        step={0.5}
                        value={cleanNum(settings.paternityPhenotypeDailyGrowth, 4.0)}
                        onChange={(e) =>
                          updateSetting(
                            'paternityPhenotypeDailyGrowth' as any,
                            Math.max(0, Math.min(20, Number(e.target.value)))
                          )
                        }
                        className="w-12 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none disabled:cursor-not-allowed"
                      />
                      <span className="text-foreground-muted text-[0.6875rem]">
                        {unitPercentPerDay}
                      </span>
                    </div>
                  </AppTooltip>
                  <span className="text-foreground-muted">)</span>
                </div>

                <span className="text-foreground-muted font-semibold text-xs">
                  {t('settings.fertility.formulaMaxCapPrefix')}
                </span>

                {/* Max Cap */}
                <AppTooltip
                  content={
                    isBirth100 ? disabledTooltip : t('settings.fertility.phenotypeMaxTooltip')
                  }
                >
                  <div
                    className={`flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] ${
                      isBirth100 ? 'cursor-not-allowed opacity-50' : 'focus-within:border-border-accent'
                    }`}
                  >
                    <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">
                      {t('settings.fertility.formulaMaxLabel')}
                    </span>
                    <input
                      type="number"
                      disabled={isBirth100}
                      min={0}
                      max={100}
                      step={1}
                      value={cleanNum(settings.paternityPhenotypeMaxChance, 80)}
                      onChange={(e) =>
                        updateSetting(
                          'paternityPhenotypeMaxChance' as any,
                          Math.max(0, Math.min(100, Number(e.target.value)))
                        )
                      }
                      className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none disabled:cursor-not-allowed"
                    />
                    <span className="text-foreground-muted">{unitPercent}</span>
                  </div>
                </AppTooltip>
              </div>

              {isBirth100 ? (
                <p className="text-[0.6875rem] text-amber-400/80">
                  {t('settings.fertility.phenotypeInactiveHelp')}
                </p>
              ) : (
                <p className="text-[0.6875rem] text-foreground-muted">
                  {t('settings.fertility.phenotypeActiveHelp')}
                </p>
              )}
            </div>
          )
        })()}
      </SectionCard>

      {settings.hasSkyrimNet && (
        <SectionCard title={t('settings.fertility.aiMethodTitle')} icon={Bot}>
          <SettingRow
            label={t('settings.fertility.optOutLlmLabel')}
            description={t('settings.fertility.optOutLlmDesc')}
          >
            <ToggleSwitch
              checked={!!settings.paternityForceDiceRolls}
              onChange={(val) => updateSetting('paternityForceDiceRolls' as any, val)}
            />
          </SettingRow>
        </SectionCard>
      )}

      <SectionCard
        title={
          settings.hasSkyrimNet
            ? settings.paternityForceDiceRolls
              ? t('settings.fertility.activeDiceTitle')
              : t('settings.fertility.fallbackDiceTitle')
            : t('settings.fertility.paternityDiceTitle')
        }
        icon={Dices}
      >
        <SettingRow
          label={t('settings.fertility.revealChanceLabel')}
          description={
            settings.hasSkyrimNet && !settings.paternityForceDiceRolls
              ? t('settings.fertility.revealChanceFallbackDesc')
              : t('settings.fertility.revealChanceDirectDesc')
          }
        >
          <SliderInput
            value={settings.paternityNonSkyrimNetRevealChance ?? 50}
            min={0}
            max={100}
            step={1}
            unit={unitPercent}
            onChange={(val) => updateSetting('paternityNonSkyrimNetRevealChance' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.fertility.breakupChanceLabel')}
          description={t('settings.fertility.breakupChanceDesc')}
        >
          <SliderInput
            value={settings.paternityBreakupChance ?? 50}
            min={0}
            max={100}
            step={1}
            unit={unitPercent}
            onChange={(val) => updateSetting('paternityBreakupChance' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.fertility.bioStepInChanceLabel')}
          description={t('settings.fertility.bioStepInChanceDesc')}
        >
          <SliderInput
            value={settings.paternityBioStepUpChance ?? 30}
            min={0}
            max={100}
            step={1}
            unit={unitPercent}
            onChange={(val) => updateSetting('paternityBioStepUpChance' as any, val)}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
