import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SelectInput } from '../shared/SelectInput'
import { SectionCard } from '../shared/SectionCard'
import { MatchmakingOrientation, MatchmakingSettings as MatchmakingSettingsType } from '../../types'
import { HeartHandshake, Bot, Globe, Dices, RotateCcw } from 'lucide-react'

const CHANCE_KEYS = [
  'diceRollChanceNewDating',
  'diceRollChanceNewEngaged',
  'diceRollChanceNewLoverAllowed',
  'diceRollChanceNewLoverCheating',
  'diceRollChanceAdvance',
  'diceRollChanceBreakup',
  'diceRollChanceOpenClose',
] as const

type DiceRollChanceKey = (typeof CHANCE_KEYS)[number]

const DEFAULT_CHANCES: Record<DiceRollChanceKey, number> = {
  diceRollChanceNewDating: 22,
  diceRollChanceNewEngaged: 17,
  diceRollChanceNewLoverAllowed: 17,
  diceRollChanceNewLoverCheating: 6,
  diceRollChanceAdvance: 22,
  diceRollChanceBreakup: 11,
  diceRollChanceOpenClose: 5,
}

export const MatchmakingSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  const settings = useMarasStore((s) => s.settings)
  const matchmakingSettings = useMarasStore((s) => s.matchmakingSettings)
  const updateMatchmakingSettings = useMarasStore((s) => s.updateMatchmakingSettings)
  const generateGlobalAiSuggestions = useMarasStore((s) => s.generateGlobalAiSuggestions)
  const setActiveTab = useMarasStore((s) => s.setActiveTab)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)

  const hasSkyrimNet = Boolean(settings.hasSkyrimNet)
  const useLLM = hasSkyrimNet && (matchmakingSettings.useLLM ?? true)
  const cooldownDays = matchmakingSettings.npcCooldownDays ?? 14

  const isKeyActive = (k: DiceRollChanceKey): boolean => {
    if (k === 'diceRollChanceNewLoverCheating') return Boolean(matchmakingSettings.allowCheating)
    if (k === 'diceRollChanceBreakup') return Boolean(matchmakingSettings.allowBreakups)
    if (k === 'diceRollChanceOpenClose') return Boolean(matchmakingSettings.allowOpenClose)
    return true
  }

  const chances: Record<DiceRollChanceKey, number> = {
    diceRollChanceNewDating: matchmakingSettings.diceRollChanceNewDating ?? 22,
    diceRollChanceNewEngaged: matchmakingSettings.diceRollChanceNewEngaged ?? 17,
    diceRollChanceNewLoverAllowed: matchmakingSettings.diceRollChanceNewLoverAllowed ?? 17,
    diceRollChanceNewLoverCheating: matchmakingSettings.allowCheating
      ? (matchmakingSettings.diceRollChanceNewLoverCheating ?? 6)
      : 0,
    diceRollChanceAdvance: matchmakingSettings.diceRollChanceAdvance ?? 22,
    diceRollChanceBreakup: matchmakingSettings.allowBreakups
      ? (matchmakingSettings.diceRollChanceBreakup ?? 11)
      : 0,
    diceRollChanceOpenClose: matchmakingSettings.allowOpenClose
      ? (matchmakingSettings.diceRollChanceOpenClose ?? 5)
      : 0,
  }

  const totalPercent = Object.keys(chances).reduce((acc, k) => {
    const key = k as DiceRollChanceKey
    return acc + (isKeyActive(key) ? chances[key] : 0)
  }, 0)

  const handleChanceChange = (changedKey: DiceRollChanceKey, rawVal: number) => {
    const newVal = Math.max(0, Math.min(100, Math.round(rawVal)))
    const activeKeys = CHANCE_KEYS.filter(isKeyActive)
    const otherActiveKeys = activeKeys.filter((k) => k !== changedKey)

    if (otherActiveKeys.length === 0) {
      updateMatchmakingSettings({ [changedKey]: 100 })
      return
    }

    if (newVal >= 100) {
      const updates: Partial<MatchmakingSettingsType> = { [changedKey]: 100 }
      for (const k of otherActiveKeys) {
        updates[k] = 0
      }
      updateMatchmakingSettings(updates)
      return
    }

    const remaining = 100 - newVal
    const otherSum = otherActiveKeys.reduce((acc, k) => acc + chances[k], 0)
    const updates: Partial<MatchmakingSettingsType> = { [changedKey]: newVal }

    if (otherSum > 0) {
      let allocated = 0
      for (let i = 0; i < otherActiveKeys.length; i++) {
        const k = otherActiveKeys[i]
        if (i === otherActiveKeys.length - 1) {
          updates[k] = Math.max(0, remaining - allocated)
        } else {
          const share = Math.round((chances[k] / otherSum) * remaining)
          updates[k] = share
          allocated += share
        }
      }
    } else {
      let allocated = 0
      for (let i = 0; i < otherActiveKeys.length; i++) {
        const k = otherActiveKeys[i]
        if (i === otherActiveKeys.length - 1) {
          updates[k] = Math.max(0, remaining - allocated)
        } else {
          const share = Math.round(remaining / otherActiveKeys.length)
          updates[k] = share
          allocated += share
        }
      }
    }

    let currentTotal = newVal
    for (const k of otherActiveKeys) {
      currentTotal += (updates[k] as number) ?? 0
    }
    const diff = 100 - currentTotal
    if (diff !== 0 && otherActiveKeys.length > 0) {
      const firstOther = otherActiveKeys[0]
      updates[firstOther] = Math.max(0, ((updates[firstOther] as number) ?? 0) + diff)
    }

    updateMatchmakingSettings(updates)
  }

  const handleToggleRule = (
    ruleKey: 'allowCheating' | 'allowBreakups' | 'allowOpenClose',
    newVal: boolean
  ) => {
    const associatedChanceKey: Record<string, DiceRollChanceKey> = {
      allowCheating: 'diceRollChanceNewLoverCheating',
      allowBreakups: 'diceRollChanceBreakup',
      allowOpenClose: 'diceRollChanceOpenClose',
    }
    const chanceKey = associatedChanceKey[ruleKey]

    if (!newVal) {
      const oldWeight = chances[chanceKey]
      const remainingActive = CHANCE_KEYS.filter((k) => k !== chanceKey && isKeyActive(k))
      const updates: Partial<MatchmakingSettingsType> = {
        [ruleKey]: false,
        [chanceKey]: 0,
      }

      if (oldWeight > 0 && remainingActive.length > 0) {
        const activeSum = remainingActive.reduce((acc, k) => acc + chances[k], 0)
        let allocated = 0
        for (let i = 0; i < remainingActive.length; i++) {
          const k = remainingActive[i]
          if (i === remainingActive.length - 1) {
            updates[k] = (chances[k] ?? 0) + (oldWeight - allocated)
          } else {
            const share =
              activeSum > 0
                ? Math.round((chances[k] / activeSum) * oldWeight)
                : Math.round(oldWeight / remainingActive.length)
            updates[k] = (chances[k] ?? 0) + share
            allocated += share
          }
        }
      }
      updateMatchmakingSettings(updates)
    } else {
      const desiredWeight =
        ruleKey === 'allowCheating' ? 6 : ruleKey === 'allowBreakups' ? 10 : 5
      const activeKeys = CHANCE_KEYS.filter(isKeyActive)
      const updates: Partial<MatchmakingSettingsType> = {
        [ruleKey]: true,
        [chanceKey]: desiredWeight,
      }
      const currentActiveSum = activeKeys.reduce((acc, k) => acc + chances[k], 0)
      if (currentActiveSum > 0) {
        let allocated = 0
        for (let i = 0; i < activeKeys.length; i++) {
          const k = activeKeys[i]
          if (i === activeKeys.length - 1) {
            updates[k] = Math.max(0, chances[k] - (desiredWeight - allocated))
          } else {
            const share = Math.round((chances[k] / currentActiveSum) * desiredWeight)
            updates[k] = Math.max(0, chances[k] - share)
            allocated += share
          }
        }
      }
      updateMatchmakingSettings(updates)
    }
  }

  const handleRestoreDefaultChances = () => {
    const activeKeys = CHANCE_KEYS.filter(isKeyActive)
    const updates: Partial<MatchmakingSettingsType> = {}

    for (const k of CHANCE_KEYS) {
      if (!isKeyActive(k)) {
        updates[k] = 0
      }
    }

    const activeSum = activeKeys.reduce((acc, k) => acc + DEFAULT_CHANCES[k], 0)
    if (activeSum === 100) {
      for (const k of activeKeys) {
        updates[k] = DEFAULT_CHANCES[k]
      }
    } else if (activeSum > 0) {
      let allocated = 0
      for (let i = 0; i < activeKeys.length; i++) {
        const k = activeKeys[i]
        if (i === activeKeys.length - 1) {
          updates[k] = Math.max(0, 100 - allocated)
        } else {
          const val = Math.round((DEFAULT_CHANCES[k] / activeSum) * 100)
          updates[k] = val
          allocated += val
        }
      }
    } else {
      updates.diceRollChanceNewDating = 100
    }

    updateMatchmakingSettings(updates)
  }

  const unitPercent = t('settings.matchmaking.unitPercent')
  const unitDaysShort = t('settings.matchmaking.unitDaysShort')

  return (
    <div className="space-y-[1rem]">
      {/* Section 1: Scheduled Auto Matchmaking */}
      <SectionCard
        title={
          useLLM
            ? t('settings.matchmaking.scheduledAiTitle')
            : t('settings.matchmaking.scheduledAutoTitle')
        }
        icon={useLLM ? Bot : Dices}
        subtitle={
          useLLM
            ? t('settings.matchmaking.scheduledAiSubtitle')
            : t('settings.matchmaking.scheduledAutoSubtitle')
        }
      >
        <SettingRow
          label={t('settings.matchmaking.autoScheduledLabel')}
          description={
            useLLM
              ? t('settings.matchmaking.autoScheduledAiDesc')
              : t('settings.matchmaking.autoScheduledDiceDesc')
          }
        >
          <ToggleSwitch
            checked={matchmakingSettings.enabled}
            onChange={(val) => updateMatchmakingSettings({ enabled: val })}
          />
        </SettingRow>

        {hasSkyrimNet && (
          <SettingRow
            label={t('settings.matchmaking.useAiLabel')}
            description={t('settings.matchmaking.useAiDesc')}
          >
            <ToggleSwitch
              checked={matchmakingSettings.useLLM ?? true}
              onChange={(val) => updateMatchmakingSettings({ useLLM: val })}
            />
          </SettingRow>
        )}

        {hasSkyrimNet && (
          <>
            <SettingRow
              label={t('settings.matchmaking.evaluationStepLabel')}
              description={t('settings.matchmaking.evaluationStepDesc')}
            >
              <ToggleSwitch
                checked={matchmakingSettings.evaluationStep ?? true}
                onChange={(val) => updateMatchmakingSettings({ evaluationStep: val })}
              />
            </SettingRow>

            <SettingRow
              label={t('settings.matchmaking.llmMemoriesLabel')}
              description={t('settings.matchmaking.llmMemoriesDesc')}
            >
              <ToggleSwitch
                checked={matchmakingSettings.generateMemories ?? true}
                onChange={(val) => updateMatchmakingSettings({ generateMemories: val })}
              />
            </SettingRow>
          </>
        )}

        <SettingRow
          label={t('settings.matchmaking.confirmResultsLabel')}
          description={t('settings.matchmaking.confirmResultsDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.confirmResults ?? false}
            onChange={(val) => updateMatchmakingSettings({ confirmResults: val })}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.matchmaking.intervalDaysLabel')}
          description={tp('settings.matchmaking.intervalDaysDesc', matchmakingSettings.intervalDays)}
        >
          <SliderInput
            value={matchmakingSettings.intervalDays}
            min={1}
            max={30}
            step={1}
            unit={unitDaysShort}
            onChange={(val) => updateMatchmakingSettings({ intervalDays: Math.round(val) })}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.matchmaking.maxSuggestionsLabel')}
          description={tp('settings.matchmaking.maxSuggestionsDesc', matchmakingSettings.maxSuggestions)}
        >
          <SliderInput
            value={matchmakingSettings.maxSuggestions}
            min={1}
            max={15}
            step={1}
            unit=""
            onChange={(val) => updateMatchmakingSettings({ maxSuggestions: Math.round(val) })}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.matchmaking.npcCooldownLabel')}
          description={tp('settings.matchmaking.npcCooldownDesc', cooldownDays)}
        >
          <SliderInput
            value={cooldownDays}
            min={1}
            max={60}
            step={1}
            unit={unitDaysShort}
            onChange={(val) => updateMatchmakingSettings({ npcCooldownDays: Math.round(val) })}
          />
        </SettingRow>
      </SectionCard>

      {/* Section 2: Dice Roll Chances (Visible when not in LLM mode) */}
      {!useLLM && (
        <SectionCard
          title={t('settings.matchmaking.diceChancesTitle')}
          icon={Dices}
          actionSlot={
            <div className="flex items-center gap-2">
              <button
                type="button"
                onClick={handleRestoreDefaultChances}
                className="flex items-center gap-1.5 text-xs px-2.5 py-1 rounded bg-panel-inset hover:bg-accent-faint text-foreground-secondary hover:text-accent-bright border border-border-subtle hover:border-border-accent transition-colors cursor-pointer"
                title={t('settings.matchmaking.restoreDefaultsTooltip')}
              >
                <RotateCcw className="w-3.5 h-3.5" />
                <span>{t('settings.matchmaking.restoreDefaultsBtn')}</span>
              </button>
              <span className="text-xs font-mono font-semibold text-emerald-400 bg-emerald-950/40 px-2 py-0.5 rounded border border-emerald-800/50">
                {t('settings.matchmaking.totalPercentBadge', { percent: totalPercent })}
              </span>
            </div>
          }
        >
          <div className="text-muted-foreground text-[0.75rem] mb-2 px-1">
            {t('settings.matchmaking.diceChancesIntro')}
          </div>

          <SettingRow
            label={t('settings.matchmaking.chanceNewDatingLabel')}
            description={t('settings.matchmaking.chanceNewDatingDesc')}
          >
            <SliderInput
              value={chances.diceRollChanceNewDating}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              onChange={(val) => handleChanceChange('diceRollChanceNewDating', val)}
            />
          </SettingRow>

          <SettingRow
            label={t('settings.matchmaking.chanceNewEngagedLabel')}
            description={t('settings.matchmaking.chanceNewEngagedDesc')}
          >
            <SliderInput
              value={chances.diceRollChanceNewEngaged}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              onChange={(val) => handleChanceChange('diceRollChanceNewEngaged', val)}
            />
          </SettingRow>

          <SettingRow
            label={t('settings.matchmaking.chanceNewLoverAllowedLabel')}
            description={t('settings.matchmaking.chanceNewLoverAllowedDesc')}
          >
            <SliderInput
              value={chances.diceRollChanceNewLoverAllowed}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              onChange={(val) => handleChanceChange('diceRollChanceNewLoverAllowed', val)}
            />
          </SettingRow>

          <SettingRow
            label={t('settings.matchmaking.chanceNewLoverCheatingLabel')}
            description={
              matchmakingSettings.allowCheating
                ? t('settings.matchmaking.chanceNewLoverCheatingDesc')
                : t('settings.matchmaking.chanceNewLoverCheatingDisabledDesc')
            }
          >
            <SliderInput
              value={chances.diceRollChanceNewLoverCheating}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              disabled={!matchmakingSettings.allowCheating}
              onChange={(val) => handleChanceChange('diceRollChanceNewLoverCheating', val)}
            />
          </SettingRow>

          <SettingRow
            label={t('settings.matchmaking.chanceAdvanceLabel')}
            description={t('settings.matchmaking.chanceAdvanceDesc')}
          >
            <SliderInput
              value={chances.diceRollChanceAdvance}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              onChange={(val) => handleChanceChange('diceRollChanceAdvance', val)}
            />
          </SettingRow>

          <SettingRow
            label={t('settings.matchmaking.chanceBreakupLabel')}
            description={
              matchmakingSettings.allowBreakups
                ? t('settings.matchmaking.chanceBreakupDesc')
                : t('settings.matchmaking.chanceBreakupDisabledDesc')
            }
          >
            <SliderInput
              value={chances.diceRollChanceBreakup}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              disabled={!matchmakingSettings.allowBreakups}
              onChange={(val) => handleChanceChange('diceRollChanceBreakup', val)}
            />
          </SettingRow>

          <SettingRow
            label={t('settings.matchmaking.chanceOpenCloseLabel')}
            description={
              matchmakingSettings.allowOpenClose
                ? t('settings.matchmaking.chanceOpenCloseDesc')
                : t('settings.matchmaking.chanceOpenCloseDisabledDesc')
            }
          >
            <SliderInput
              value={chances.diceRollChanceOpenClose}
              min={0}
              max={100}
              step={1}
              unit={unitPercent}
              disabled={!matchmakingSettings.allowOpenClose}
              onChange={(val) => handleChanceChange('diceRollChanceOpenClose', val)}
            />
          </SettingRow>
        </SectionCard>
      )}

      {/* Section 3: Matchmaking Rules & Constraints */}
      <SectionCard title={t('settings.matchmaking.rulesTitle')} icon={HeartHandshake}>
        <SettingRow
          label={t('settings.matchmaking.orientationLabel')}
          description={t('settings.matchmaking.orientationDesc')}
        >
          <SelectInput
            value={matchmakingSettings.orientation ?? MatchmakingOrientation.Heterosexual}
            options={[
              { value: MatchmakingOrientation.Any, label: t('settings.matchmaking.orientationAny') },
              {
                value: MatchmakingOrientation.Heterosexual,
                label: t('settings.matchmaking.orientationHeterosexual'),
              },
              {
                value: MatchmakingOrientation.Homosexual,
                label: t('settings.matchmaking.orientationHomosexual'),
              },
              { value: MatchmakingOrientation.Gay, label: t('settings.matchmaking.orientationGay') },
              {
                value: MatchmakingOrientation.Lesbian,
                label: t('settings.matchmaking.orientationLesbian'),
              },
              {
                value: MatchmakingOrientation.HeteroAndGay,
                label: t('settings.matchmaking.orientationHeteroAndGay'),
              },
              {
                value: MatchmakingOrientation.HeteroAndLesbian,
                label: t('settings.matchmaking.orientationHeteroAndLesbian'),
              },
            ]}
            onChange={(val) => updateMatchmakingSettings({ orientation: parseInt(val, 10) })}
            className="min-w-[18rem] max-w-[24rem] w-auto text-xs"
          />
        </SettingRow>

        <SettingRow
          label={t('settings.matchmaking.allowCheatingLabel')}
          description={t('settings.matchmaking.allowCheatingDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.allowCheating}
            onChange={(val) => handleToggleRule('allowCheating', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.matchmaking.allowBreakupsLabel')}
          description={t('settings.matchmaking.allowBreakupsDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.allowBreakups}
            onChange={(val) => handleToggleRule('allowBreakups', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.matchmaking.allowOpenCloseLabel')}
          description={t('settings.matchmaking.allowOpenCloseDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.allowOpenClose}
            onChange={(val) => handleToggleRule('allowOpenClose', val)}
          />
        </SettingRow>
      </SectionCard>

      {/* Section 5: Manual Matchmaking Actions */}
      <SectionCard
        title={t('settings.matchmaking.manualActionsTitle')}
        icon={Globe}
        subtitle={t('settings.matchmaking.manualActionsSubtitle')}
      >
        <SettingRow
          label={t('settings.matchmaking.manualWorldScanLabel')}
          description={t('settings.matchmaking.manualWorldScanDesc')}
        >
          <button
            type="button"
            onClick={() => {
              generateGlobalAiSuggestions(false)
              setActiveTab('matchmaking')
              setActiveSubSection('llmMatch')
            }}
            disabled={!settings.hasSkyrimNet}
            className="px-[0.875rem] py-[0.375rem] bg-accent hover:bg-accent-bright disabled:opacity-50 text-panel font-semibold text-xs rounded transition-colors cursor-pointer flex items-center gap-[0.375rem]"
          >
            <Globe className="w-[0.875em] h-[0.875em]" />
            <span>{t('settings.matchmaking.runWorldScanBtn')}</span>
          </button>
        </SettingRow>
      </SectionCard>
    </div>
  )
}
