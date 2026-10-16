import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { AppTooltip } from '../shared/AppTooltip'
import { Eye, Dices, Bot } from 'lucide-react'

export const FertilitySettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="Paternity Daily Evaluation Formulas (Shared)" icon={Eye}>
        {/* Subtle Physical Traits Formula */}
        <div className="p-[0.75rem] bg-panel-inset/80 border border-border-subtle rounded-lg space-y-[0.625rem]">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-foreground-primary">Subtle Physical Trait Formula</span>
            <span className="text-[0.6875rem] font-mono text-accent-bright bg-accent/10 px-[0.375rem] py-[0.125rem] rounded border border-accent/20">
              Base + (Days × Daily) ≤ Max
            </span>
          </div>

          <div className="flex flex-wrap items-center gap-[0.5rem] text-xs font-mono">
            {/* Base */}
            <AppTooltip content="Initial discovery probability on day 0 of birth (0–100%)">
              <div className="flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus-within:border-border-accent">
                <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">Base</span>
                <input
                  type="number"
                  min={0}
                  max={100}
                  step={1}
                  value={settings.paternityBaseChance ?? 2}
                  onChange={(e) => updateSetting('paternityBaseChance' as any, Math.max(0, Math.min(100, Number(e.target.value))))}
                  className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none"
                />
                <span className="text-foreground-muted">%</span>
              </div>
            </AppTooltip>

            <span className="text-foreground-muted font-bold text-sm">+</span>

            {/* (Days x Daily Growth) */}
            <div className="flex items-center gap-[0.375rem] bg-panel-raised/50 border border-border-subtle/60 rounded px-[0.5rem] py-[0.25rem]">
              <span className="text-foreground-muted">(</span>
              <span className="text-accent text-[0.6875rem] font-sans font-medium">Days</span>
              <span className="text-foreground-muted">×</span>
              <AppTooltip content="Daily probability increase added per day since birth (e.g. 2.0% / day)">
                <div className="flex items-center gap-[0.125rem] bg-panel-raised border border-border-subtle rounded px-[0.375rem] py-[0.125rem] focus-within:border-border-accent">
                  <input
                    type="number"
                    min={0}
                    max={20}
                    step={0.5}
                    value={settings.paternityDailyGrowth ?? 2.0}
                    onChange={(e) => updateSetting('paternityDailyGrowth' as any, Math.max(0, Math.min(20, Number(e.target.value))))}
                    className="w-12 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none"
                  />
                  <span className="text-foreground-muted text-[0.6875rem]">%/day</span>
                </div>
              </AppTooltip>
              <span className="text-foreground-muted">)</span>
            </div>

            <span className="text-foreground-muted font-semibold text-xs">, max cap:</span>

            {/* Max Cap */}
            <AppTooltip content="Maximum upper probability cap for subtle discovery (0–100%)">
              <div className="flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus-within:border-border-accent">
                <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">Max</span>
                <input
                  type="number"
                  min={0}
                  max={100}
                  step={1}
                  value={settings.paternityMaxChance ?? 60}
                  onChange={(e) => updateSetting('paternityMaxChance' as any, Math.max(0, Math.min(100, Number(e.target.value))))}
                  className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none"
                />
                <span className="text-foreground-muted">%</span>
              </div>
            </AppTooltip>
          </div>

          <p className="text-[0.6875rem] text-foreground-muted">
            Standard subtle tick: gradual physical trait resemblance increases each day the child grows.
          </p>
        </div>

        {/* Phenotype Mismatch Birth Evaluation Chance */}
        <SettingRow
          label="Phenotype Mismatch Birth Evaluation Chance"
          description="Probability to immediately trigger a paternity secret evaluation upon birth when child has distinct racial/visual discrepancies with presumed father."
        >
          <SliderInput
            value={settings.paternityPhenotypeBirthChance ?? 100}
            min={0}
            max={100}
            step={1}
            unit="%"
            onChange={(val) => updateSetting('paternityPhenotypeBirthChance' as any, val)}
          />
        </SettingRow>

        {/* Phenotype Mismatch Formula */}
        {(() => {
          const isBirth100 = (settings.paternityPhenotypeBirthChance ?? 100) >= 100
          return (
            <div className={`p-[0.75rem] bg-panel-inset/80 border border-border-subtle rounded-lg space-y-[0.625rem] transition-opacity ${isBirth100 ? 'opacity-60' : ''}`}>
              <div className="flex items-center justify-between">
                <span className="text-xs font-semibold text-foreground-primary">Phenotype Mismatch Daily Formula</span>
                {isBirth100 ? (
                  <span className="text-[0.6875rem] font-mono text-amber-400 bg-amber-500/10 px-[0.375rem] py-[0.125rem] rounded border border-amber-500/20" title="Because birth evaluation chance is 100%, phenotype secrets are evaluated immediately upon birth and will not trigger daily growth.">
                    Inactive (Birth Chance is 100%)
                  </span>
                ) : (
                  <span className="text-[0.6875rem] font-mono text-purple-400 bg-purple-500/10 px-[0.375rem] py-[0.125rem] rounded border border-purple-500/20">
                    Base + (Days × Daily) ≤ Max
                  </span>
                )}
              </div>

              <div className="flex flex-wrap items-center gap-[0.5rem] text-xs font-mono">
                {/* Base */}
                <AppTooltip content={isBirth100 ? "Disabled: secret evaluates immediately at birth (100%)" : "Base starting probability when distinct visual/racial discrepancy exists (0–100%)"}>
                  <div className={`flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] ${isBirth100 ? 'cursor-not-allowed opacity-50' : 'focus-within:border-border-accent'}`}>
                    <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">Base</span>
                    <input
                      type="number"
                      disabled={isBirth100}
                      min={0}
                      max={100}
                      step={1}
                      value={settings.paternityPhenotypeBaseChance ?? 15}
                      onChange={(e) => updateSetting('paternityPhenotypeBaseChance' as any, Math.max(0, Math.min(100, Number(e.target.value))))}
                      className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none disabled:cursor-not-allowed"
                    />
                    <span className="text-foreground-muted">%</span>
                  </div>
                </AppTooltip>

                <span className="text-foreground-muted font-bold text-sm">+</span>

                {/* (Days x Daily Growth) */}
                <div className={`flex items-center gap-[0.375rem] bg-panel-raised/50 border border-border-subtle/60 rounded px-[0.5rem] py-[0.25rem] ${isBirth100 ? 'opacity-50' : ''}`}>
                  <span className="text-foreground-muted">(</span>
                  <span className="text-accent text-[0.6875rem] font-sans font-medium">Days</span>
                  <span className="text-foreground-muted">×</span>
                  <AppTooltip content={isBirth100 ? "Disabled: secret evaluates immediately at birth (100%)" : "Daily probability increase when visual differences become more glaring each day (e.g. 4.0% / day)"}>
                    <div className={`flex items-center gap-[0.125rem] bg-panel-raised border border-border-subtle rounded px-[0.375rem] py-[0.125rem] ${isBirth100 ? 'cursor-not-allowed' : 'focus-within:border-border-accent'}`}>
                      <input
                        type="number"
                        disabled={isBirth100}
                        min={0}
                        max={20}
                        step={0.5}
                        value={settings.paternityPhenotypeDailyGrowth ?? 4.0}
                        onChange={(e) => updateSetting('paternityPhenotypeDailyGrowth' as any, Math.max(0, Math.min(20, Number(e.target.value))))}
                        className="w-12 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none disabled:cursor-not-allowed"
                      />
                      <span className="text-foreground-muted text-[0.6875rem]">%/day</span>
                    </div>
                  </AppTooltip>
                  <span className="text-foreground-muted">)</span>
                </div>

                <span className="text-foreground-muted font-semibold text-xs">, max cap:</span>

                {/* Max Cap */}
                <AppTooltip content={isBirth100 ? "Disabled: secret evaluates immediately at birth (100%)" : "Maximum upper probability cap for phenotype mismatch (0–100%)"}>
                  <div className={`flex items-center gap-[0.25rem] bg-panel-raised border border-border-subtle rounded px-[0.5rem] py-[0.25rem] ${isBirth100 ? 'cursor-not-allowed opacity-50' : 'focus-within:border-border-accent'}`}>
                    <span className="text-[0.625rem] text-foreground-muted uppercase tracking-wider font-sans">Max</span>
                    <input
                      type="number"
                      disabled={isBirth100}
                      min={0}
                      max={100}
                      step={1}
                      value={settings.paternityPhenotypeMaxChance ?? 80}
                      onChange={(e) => updateSetting('paternityPhenotypeMaxChance' as any, Math.max(0, Math.min(100, Number(e.target.value))))}
                      className="w-11 bg-transparent text-right font-semibold text-foreground-primary focus:outline-none disabled:cursor-not-allowed"
                    />
                    <span className="text-foreground-muted">%</span>
                  </div>
                </AppTooltip>
              </div>

              {isBirth100 ? (
                <p className="text-[0.6875rem] text-amber-400/80">
                  Daily formula is inactive because Birth Evaluation Chance is set to 100% (paternity secrets are evaluated immediately at birth). Lower the birth evaluation chance above to allow daily growth.
                </p>
              ) : (
                <p className="text-[0.6875rem] text-foreground-muted">
                  Accelerated daily tick: glaring visual differences between biological and presumed father become noticeable much faster.
                </p>
              )}
            </div>
          )
        })()}
      </SectionCard>

      {settings.hasSkyrimNet && (
        <SectionCard title="SkyrimNet AI Evaluation Method" icon={Bot}>
          <SettingRow
            label="Opt Out of LLM (Use Dice Rolls)"
            description="When enabled, bypasses SkyrimNet LLM evaluation for paternity secrets and uses the deterministic dice-roll method below."
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
            ? (settings.paternityForceDiceRolls
              ? "Active Dice-Roll Evaluation Rules"
              : "Fallback Dice-Roll Evaluation Rules")
            : "Paternity Dice-Roll Evaluation Rules"
        }
        icon={Dices}
      >
        <SettingRow
          label="Paternity Reveal Chance"
          description={
            settings.hasSkyrimNet && !settings.paternityForceDiceRolls
              ? "Probability that paternity secret is revealed when fallback triggers (e.g. if SkyrimNet evaluation fails). Phenotype mismatches always reveal immediately (100%)."
              : "Probability that paternity secret is revealed when evaluation triggers without LLM. Phenotype mismatches always reveal immediately (100%)."
          }
        >
          <SliderInput
            value={settings.paternityNonSkyrimNetRevealChance ?? 50}
            min={0}
            max={100}
            step={1}
            unit="%"
            onChange={(val) => updateSetting('paternityNonSkyrimNetRevealChance' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label="Presumed Father Breakup / Divorce Chance"
          description="When paternity secret is revealed, probability that the presumed father breaks up or divorces the mother upon discovering he is not the biological father."
        >
          <SliderInput
            value={settings.paternityBreakupChance ?? 50}
            min={0}
            max={100}
            step={1}
            unit="%"
            onChange={(val) => updateSetting('paternityBreakupChance' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label="Biological Father Step-In Chance"
          description="If breakup/divorce occurs and the biological father is available to marry, probability that he steps in, marries the mother, and becomes the child's recognized parent."
        >
          <SliderInput
            value={settings.paternityBioStepUpChance ?? 30}
            min={0}
            max={100}
            step={1}
            unit="%"
            onChange={(val) => updateSetting('paternityBioStepUpChance' as any, val)}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
