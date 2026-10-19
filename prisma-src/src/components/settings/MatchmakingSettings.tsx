import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
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
  const settings = useMarasStore((s) => s.settings)
  const matchmakingSettings = useMarasStore((s) => s.matchmakingSettings)
  const updateMatchmakingSettings = useMarasStore((s) => s.updateMatchmakingSettings)
  const generateGlobalAiSuggestions = useMarasStore((s) => s.generateGlobalAiSuggestions)
  const setActiveTab = useMarasStore((s) => s.setActiveTab)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)

  const hasSkyrimNet = Boolean(settings.hasSkyrimNet)
  const useLLM = hasSkyrimNet && (matchmakingSettings.useLLM ?? true)

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

  return (
    <div className="space-y-[1rem]">
      {/* Section 1: Scheduled Auto Matchmaking */}
      <SectionCard
        title={useLLM ? 'Scheduled AI Matchmaking' : 'Scheduled Auto Matchmaking'}
        icon={useLLM ? Bot : Dices}
        subtitle={
          useLLM
            ? 'Evaluates romantic prospects in the background using SkyrimNet LLM'
            : 'Evaluates romantic prospects in the background using algorithmic Dice Roll chances'
        }
      >
        <SettingRow
          label="Auto Scheduled Matchmaking"
          description={
            useLLM
              ? 'Periodically evaluate the world NPC roster in the background using SkyrimNet LLM.'
              : 'Periodically evaluate the world NPC roster in the background using algorithmic Dice Roll probability.'
          }
        >
          <ToggleSwitch
            checked={matchmakingSettings.enabled}
            onChange={(val) => updateMatchmakingSettings({ enabled: val })}
          />
        </SettingRow>

        {hasSkyrimNet && (
          <SettingRow
            label="Use SkyrimNet AI Matchmaking"
            description="When enabled, scheduled matchmaking uses SkyrimNet AI to evaluate pairings. When disabled, the algorithmic Dice Roll system is used instead (with AI verification and memory generation)."
          >
            <ToggleSwitch
              checked={matchmakingSettings.useLLM ?? true}
              onChange={(val) => updateMatchmakingSettings({ useLLM: val })}
            />
          </SettingRow>
        )}

        {useLLM && (
          <>
            <SettingRow
              label="Evaluation Step"
              description="When enabled, candidate pairs from scheduled matchmaking undergo a secondary LLM narrative evaluation before being applied."
            >
              <ToggleSwitch
                checked={matchmakingSettings.evaluationStep ?? true}
                onChange={(val) => updateMatchmakingSettings({ evaluationStep: val })}
              />
            </SettingRow>

            <SettingRow
              label="Generate Memories"
              description="When enabled, generates AI memories for NPCs involved in scheduled matchmaking relationship changes."
            >
              <ToggleSwitch
                checked={matchmakingSettings.generateMemories ?? true}
                onChange={(val) => updateMatchmakingSettings({ generateMemories: val })}
              />
            </SettingRow>
          </>
        )}

        <SettingRow
          label="Review Matchmaking Results in Modal"
          description="When enabled, presents an interactive checklist modal to review, adjust, or reject proposed NPC pairings before they are applied in-game."
        >
          <ToggleSwitch
            checked={matchmakingSettings.confirmResults ?? false}
            onChange={(val) => updateMatchmakingSettings({ confirmResults: val })}
          />
        </SettingRow>

        <SettingRow
          label="Evaluation Interval"
          description={`Number of in-game days between automated matchmaking evaluations (${matchmakingSettings.intervalDays} days).`}
        >
          <SliderInput
            value={matchmakingSettings.intervalDays}
            min={1}
            max={30}
            step={1}
            unit="d"
            onChange={(val) => updateMatchmakingSettings({ intervalDays: Math.round(val) })}
          />
        </SettingRow>

        <SettingRow
          label="Max Suggestions Per Cycle"
          description={`Maximum number of NPC pair changes proposed per automated run (${matchmakingSettings.maxSuggestions} suggestions).`}
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
          label="NPC Relationship Cooldown"
          description={`Number of in-game days an NPC must wait after a scheduled relationship change before they can be changed again (${matchmakingSettings.npcCooldownDays ?? 14} days).`}
        >
          <SliderInput
            value={matchmakingSettings.npcCooldownDays ?? 14}
            min={1}
            max={60}
            step={1}
            unit="d"
            onChange={(val) => updateMatchmakingSettings({ npcCooldownDays: Math.round(val) })}
          />
        </SettingRow>

        <SettingRow
          label="Exclude Player's Active Main Partners"
          description="Do not include NPCs who are currently dating, engaged to, or married to the player in scheduled world matchmaking."
        >
          <ToggleSwitch
            checked={matchmakingSettings.excludePlayerActivePartners ?? false}
            onChange={(val) => updateMatchmakingSettings({ excludePlayerActivePartners: val })}
          />
        </SettingRow>
      </SectionCard>

      {/* Section 2: Dice Roll Chances (Visible when not in LLM mode) */}
      {!useLLM && (
        <SectionCard
          title="Dice Roll Matchmaking Chances"
          icon={Dices}
          actionSlot={
            <div className="flex items-center gap-2">
              <button
                type="button"
                onClick={handleRestoreDefaultChances}
                className="flex items-center gap-1.5 text-xs px-2.5 py-1 rounded bg-panel-inset hover:bg-accent-faint text-foreground-secondary hover:text-accent-bright border border-border-subtle hover:border-border-accent transition-colors cursor-pointer"
                title="Restore default chance distribution"
              >
                <RotateCcw className="w-3.5 h-3.5" />
                <span>Restore Defaults</span>
              </button>
              <span className="text-xs font-mono font-semibold text-emerald-400 bg-emerald-950/40 px-2 py-0.5 rounded border border-emerald-800/50">
                Total: {totalPercent}%
              </span>
            </div>
          }
        >
          <div className="text-muted-foreground text-[0.75rem] mb-2 px-1">
            Configure the relative probability weights for each matchmaking action. Sliders auto-normalize to ensure the total is always 100%.
          </div>

          <SettingRow
            label="New Dating Pair"
            description="Probability of initiating a new Dating relationship between two eligible single NPCs."
          >
            <SliderInput
              value={chances.diceRollChanceNewDating}
              min={0}
              max={100}
              step={1}
              unit="%"
              onChange={(val) => handleChanceChange('diceRollChanceNewDating', val)}
            />
          </SettingRow>

          <SettingRow
            label="New Engaged Pair"
            description="Probability of initiating a direct Engagement between two eligible single NPCs."
          >
            <SliderInput
              value={chances.diceRollChanceNewEngaged}
              min={0}
              max={100}
              step={1}
              unit="%"
              onChange={(val) => handleChanceChange('diceRollChanceNewEngaged', val)}
            />
          </SettingRow>

          <SettingRow
            label="New Lover (Allowed / Open)"
            description="Probability of establishing a secondary Lover relationship where partners allow it (single or open main pair)."
          >
            <SliderInput
              value={chances.diceRollChanceNewLoverAllowed}
              min={0}
              max={100}
              step={1}
              unit="%"
              onChange={(val) => handleChanceChange('diceRollChanceNewLoverAllowed', val)}
            />
          </SettingRow>

          <SettingRow
            label="New Lover (Cheating / Infidelity)"
            description={
              matchmakingSettings.allowCheating
                ? 'Probability of establishing a secondary Lover relationship that constitutes infidelity (closed main pair).'
                : 'Disabled because Allow Cheating / Infidelity is turned off below.'
            }
          >
            <SliderInput
              value={chances.diceRollChanceNewLoverCheating}
              min={0}
              max={100}
              step={1}
              unit="%"
              disabled={!matchmakingSettings.allowCheating}
              onChange={(val) => handleChanceChange('diceRollChanceNewLoverCheating', val)}
            />
          </SettingRow>

          <SettingRow
            label="Advance Existing Relationship"
            description="Probability of progressing an existing relationship (Dating to Engaged, Engaged to Married, Lover to Dating)."
          >
            <SliderInput
              value={chances.diceRollChanceAdvance}
              min={0}
              max={100}
              step={1}
              unit="%"
              onChange={(val) => handleChanceChange('diceRollChanceAdvance', val)}
            />
          </SettingRow>

          <SettingRow
            label="Breakup / Divorce"
            description={
              matchmakingSettings.allowBreakups
                ? 'Probability of dissolving an existing relationship (Dating to Ex-Dating, Married to Divorced, etc.).'
                : 'Disabled because Allow AI Breakups is turned off below.'
            }
          >
            <SliderInput
              value={chances.diceRollChanceBreakup}
              min={0}
              max={100}
              step={1}
              unit="%"
              disabled={!matchmakingSettings.allowBreakups}
              onChange={(val) => handleChanceChange('diceRollChanceBreakup', val)}
            />
          </SettingRow>

          <SettingRow
            label="Open / Close Status"
            description={
              matchmakingSettings.allowOpenClose
                ? 'Probability of modifying relationship exclusivity (opening an exclusive pair or closing an open pair).'
                : 'Disabled because Allow Open / Close Modifications is turned off below.'
            }
          >
            <SliderInput
              value={chances.diceRollChanceOpenClose}
              min={0}
              max={100}
              step={1}
              unit="%"
              disabled={!matchmakingSettings.allowOpenClose}
              onChange={(val) => handleChanceChange('diceRollChanceOpenClose', val)}
            />
          </SettingRow>
        </SectionCard>
      )}

      {/* Section 3: Matchmaking Rules & Constraints */}
      <SectionCard title="Matchmaking Rules & Constraints" icon={HeartHandshake}>
        <SettingRow
          label="Pairing Orientation"
          description="Constrain partner suggestions to specific gender combinations across automated and manual evaluation."
        >
          <SelectInput
            value={matchmakingSettings.orientation ?? MatchmakingOrientation.Heterosexual}
            options={[
              { value: MatchmakingOrientation.Any, label: 'Any (All Orientations)' },
              { value: MatchmakingOrientation.Heterosexual, label: 'Heterosexual (Male + Female)' },
              { value: MatchmakingOrientation.Homosexual, label: 'Homosexual (Same-Sex)' },
              { value: MatchmakingOrientation.Gay, label: 'Gay (Male + Male)' },
              { value: MatchmakingOrientation.Lesbian, label: 'Lesbian (Female + Female)' },
              { value: MatchmakingOrientation.HeteroAndGay, label: 'Hetero + Gay (Male + Female, Male + Male)' },
              { value: MatchmakingOrientation.HeteroAndLesbian, label: 'Hetero + Lesbian (Male + Female, Female + Female)' },
            ]}
            onChange={(val) => updateMatchmakingSettings({ orientation: parseInt(val, 10) })}
            className="w-[18rem] text-xs"
          />
        </SettingRow>

        <SettingRow
          label="Allow Cheating / Infidelity"
          description="Permit proposing secondary lover relationships for NPCs who are already in exclusive/closed main partnerships."
        >
          <ToggleSwitch
            checked={matchmakingSettings.allowCheating}
            onChange={(val) => handleToggleRule('allowCheating', val)}
          />
        </SettingRow>

        <SettingRow
          label="Allow AI Breakups"
          description="Permit proposing breakups, divorces, and separations for existing relationships when appropriate."
        >
          <ToggleSwitch
            checked={matchmakingSettings.allowBreakups}
            onChange={(val) => handleToggleRule('allowBreakups', val)}
          />
        </SettingRow>

        <SettingRow
          label="Allow Open / Close Modifications"
          description="Permit proposing opening exclusive relationships or making open relationships exclusive."
        >
          <ToggleSwitch
            checked={matchmakingSettings.allowOpenClose}
            onChange={(val) => handleToggleRule('allowOpenClose', val)}
          />
        </SettingRow>
      </SectionCard>

      {/* Section 4: Manual Matchmaking Actions */}
      <SectionCard
        title="Manual Matchmaking Actions"
        icon={Globe}
        subtitle="Trigger immediate AI evaluations across all Skyrim NPCs without waiting for interval timers"
      >
        <SettingRow
          label="Manual World Scan"
          description="Evaluate romantic prospects across all Skyrim NPCs immediately and open the AI dashboard to review and apply matches."
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
            <span>Run World Scan Now</span>
          </button>
        </SettingRow>
      </SectionCard>
    </div>
  )
}
