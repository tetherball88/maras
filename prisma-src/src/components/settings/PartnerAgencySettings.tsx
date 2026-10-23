import React from 'react'
import { Heart, Sliders, Clock } from 'lucide-react'
import { useMarasStore } from '../../store/useMarasStore'
import { SectionCard } from '../shared/SectionCard'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'

export const PartnerAgencySettings: React.FC = () => {
  const matchmakingSettings = useMarasStore((s) => s.matchmakingSettings)
  const settings = useMarasStore((s) => s.settings)
  const updateMatchmakingSettings = useMarasStore((s) => s.updateMatchmakingSettings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const hasSkyrimNet = Boolean(settings.hasSkyrimNet) && !Boolean(settings.simulateNoSkyrimNet)
  const lonelinessDays = matchmakingSettings.lonelinessThresholdDays ?? settings.lonelinessThresholdDays ?? 2

  const fmtDays = (mult: number) => {
    const days = lonelinessDays * mult
    return Number.isInteger(days) ? `${days}d` : `${days.toFixed(1)}d`
  }

  const multScanInterval = matchmakingSettings.multPartnerAgencyScanInterval ?? 1.5
  const multLonelinessDecay = matchmakingSettings.multLonelinessDecayStart ?? 1.0
  const multRecentAttention = matchmakingSettings.multRecentAttentionWindow ?? 1.0
  const multVeryRecentVisit = matchmakingSettings.multVeryRecentVisitWindow ?? 0.5
  const multNeglectGrowth = matchmakingSettings.multNeglectGrowthStart ?? 1.0
  const multContentNeglect = matchmakingSettings.multContentNeglectStart ?? 2.0
  const multProlongedAbsence = matchmakingSettings.multProlongedAbsencePenalty ?? 2.0
  const multTalkingEscalation = matchmakingSettings.multAffairTalkingEscalation ?? 2.0
  const multMinSideFlingAge = matchmakingSettings.multMinSideFlingAgeForBreakup ?? 1.5
  const multIntentTTL = matchmakingSettings.multPartnerIntentTTL ?? 2.0
  const multHomeCooldown = matchmakingSettings.multHomeArrivalCooldown ?? 1.0
  const multAffairFatigue = matchmakingSettings.multAffairFatigueStart ?? 5.0
  const multRomanticAttended = matchmakingSettings.multRomanticAttendedBreakup ?? 1.5
  const multSuggestOpenNeglect = matchmakingSettings.multSuggestOpenNeglectMin ?? 2.0

  return (
    <div className="flex flex-col gap-[1rem]">
      {/* Section 1: Core Partner Agency Toggles */}
      <SectionCard
        title="Player Partner Agency & Infidelity Engine"
        icon={Heart}
        subtitle="Dedicated Tier 2 agency (2-stage affairs, open-relationship lovers, breakups, force-greet intents & home-arrival events) for all active player main partners"
      >
        <SettingRow
          label="Enable Partner Cheating Agency"
          description="Independently evaluate the player's closed-relationship partners (Dating, Engaged, Married) for secret infidelity (2-stage Talking/Physical affairs, affair breakups, confessions, and home-arrival cheating scenarios), regardless of whether generic NPC matchmaking is enabled."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingAgencyEnabled ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingAgencyEnabled: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Partner Open-Relationship Agency"
          description="Independently evaluate the player's partners for open-relationship agency (seeking consensual side lovers, open-lover breakups, proposing to open or close the relationship, and home-arrival open-relationship scenarios), regardless of whether generic NPC matchmaking is enabled."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerOpenRelAgencyEnabled ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerOpenRelAgencyEnabled: val })}
          />
        </SettingRow>

        {hasSkyrimNet && (
          <SettingRow
            label="Use SkyrimNet AI Evaluation (Stage 2)"
            description="When a partner passes a Stage 1 probability check, ask SkyrimNet AI to evaluate whether they act on it and pick the most fitting local candidate. When disabled, weighted dice rolls select from the shortlisted candidates."
          >
            <ToggleSwitch
              checked={matchmakingSettings.playerPartnerCheatingUseLLM ?? true}
              onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingUseLLM: val })}
            />
          </SettingRow>
        )}

        <SettingRow
          label="Review Partner Agency Results in Modal"
          description="Present an interactive confirmation modal before applying any new side-lover relationship or breakup initiated by a player partner."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingConfirmResults ?? false}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingConfirmResults: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Partner Force-Greet Intents"
          description={`Allow player partners to queue relationship conversations (suggesting an open or closed relationship, or confessing guilt after ending an affair; expires after ${fmtDays(multIntentTTL)} [${multIntentTTL}x L] if ignored).`}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerAgencyEnableForceGreets ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerAgencyEnableForceGreets: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Home-Arrival Events & Letters"
          description="Evaluate active side-lover scenarios and handwritten note modals (open-lover inn dates, suspicious talking-stage letters, or in-house encounters) when arriving at a partner's home."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerAgencyEnableHomeArrivalEvents ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerAgencyEnableHomeArrivalEvents: val })}
          />
        </SettingRow>
      </SectionCard>

      {/* Section 2: Temptation, Desire & Breakup Rules */}
      <SectionCard
        title="Temptation, Desire & Breakup Rules"
        icon={Sliders}
        subtitle="Configure probability multipliers, roll caps, and loyalty anchors for player partners"
      >
        <SettingRow
          label="Global Temptation Multiplier"
          description={`Scales calculated Stage 1 cheating temptation probabilities across all closed player partners (${(matchmakingSettings.playerPartnerCheatingChanceMultiplier ?? 1.0).toFixed(1)}x).`}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerCheatingChanceMultiplier ?? 1.0}
            min={0.1}
            max={3.0}
            step={0.1}
            unit="x"
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingChanceMultiplier: Number(val.toFixed(1)) })}
          />
        </SettingRow>

        <SettingRow
          label="Open Lover Desire Multiplier"
          description={`Scales Stage 1 desire for open-relationship player partners to pursue consensual side lovers (${(matchmakingSettings.playerPartnerAgencyOpenLoverChanceMultiplier ?? 1.0).toFixed(1)}x).`}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerAgencyOpenLoverChanceMultiplier ?? 1.0}
            min={0.1}
            max={3.0}
            step={0.1}
            unit="x"
            onChange={(val) => updateMatchmakingSettings({ playerPartnerAgencyOpenLoverChanceMultiplier: Number(val.toFixed(1)) })}
          />
        </SettingRow>

        <SettingRow
          label="Side-Lover Breakup Multiplier"
          description={`Scales probability of player partners ending existing secret affairs or open-relationship side flings after ${fmtDays(multMinSideFlingAge)} (${(matchmakingSettings.playerPartnerAgencyBreakupChanceMultiplier ?? 1.0).toFixed(1)}x).`}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerAgencyBreakupChanceMultiplier ?? 1.0}
            min={0.1}
            max={3.0}
            step={0.1}
            unit="x"
            onChange={(val) => updateMatchmakingSettings({ playerPartnerAgencyBreakupChanceMultiplier: Number(val.toFixed(1)) })}
          />
        </SettingRow>

        <SettingRow
          label="Max Temptation Roll Cap"
          description={`Maximum Stage 1 temptation probability per check (${matchmakingSettings.playerPartnerCheatingMaxRollCap ?? 65}%).`}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerCheatingMaxRollCap ?? 65}
            min={5}
            max={100}
            step={5}
            unit="%"
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingMaxRollCap: Math.round(val) })}
          />
        </SettingRow>

        <SettingRow
          label="Max Secret Lovers Per Partner"
          description={`Maximum concurrent ongoing secret affair lovers a single player partner can have (${matchmakingSettings.playerPartnerCheatingMaxSecretLovers ?? 1}).`}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerCheatingMaxSecretLovers ?? 1}
            min={1}
            max={3}
            step={1}
            unit=""
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingMaxSecretLovers: Math.round(val) })}
          />
        </SettingRow>

        <SettingRow
          label="Allow Infidelity When Happy (75–100 Affection)"
          description="When disabled, partners in the Happy affection tier are completely immune unless Revenge (player caught cheating) is triggered."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingAllowWhenHappy ?? false}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingAllowWhenHappy: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Neglect Penalty"
          description={`Increase temptation when the player goes several days without meaningful affection interaction (starts after ${fmtDays(multNeglectGrowth)} [${multNeglectGrowth}x L] for Troubled/Estranged partners, or after ${fmtDays(multContentNeglect)} [${multContentNeglect}x L] for Content partners).`}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableNeglect ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingEnableNeglect: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Revenge Penalty (Player Infidelity)"
          description="Significantly increase temptation (+20%, amplified by Jealous/Proud temperaments) when the partner has an ongoing exposed affair against them by the player."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableRevenge ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingEnableRevenge: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Polygamy Hierarchy Factor"
          description="Apply a minor temptation increase for lower-ranked spouses in polygamous households (Rank 1: 0%, Rank 2: +3%, Rank 3: +5%, Rank 4+: +7%)."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableHierarchy ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingEnableHierarchy: val })}
          />
        </SettingRow>

        <SettingRow
          label="Enable Shared Children Loyalty Anchor"
          description="Reduce temptation (-12% if sharing children with the player, and hard-immune while pregnant) to reflect family commitment."
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableChildrenAnchor ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingEnableChildrenAnchor: val })}
          />
        </SettingRow>
      </SectionCard>

      {/* Section 3: Loneliness & Partner Agency Day-Threshold Multipliers */}
      <SectionCard
        title="Day-Threshold Multipliers (Relative to Loneliness Threshold L)"
        icon={Clock}
        subtitle={`All Partner Agency and Affection time windows scale from Base Loneliness Threshold (L = ${lonelinessDays}d)`}
      >
        <SettingRow
          label="Base Loneliness Threshold (L)"
          description={`Master time unit (L = ${lonelinessDays} days) from which all Partner Agency and Affection day thresholds below are calculated.`}
        >
          <SliderInput
            value={lonelinessDays}
            min={1}
            max={14}
            step={1}
            unit="d"
            onChange={(val) => {
              const days = Math.round(val)
              updateSetting('lonelinessThresholdDays', days)
              updateMatchmakingSettings({ lonelinessThresholdDays: days })
            }}
          />
        </SettingRow>

        <SettingRow
          label="Partner Agency Scan Interval"
          description={`How often scheduled Partner Agency evaluations run (${multScanInterval}x L = ${fmtDays(multScanInterval)}).`}
        >
          <SliderInput
            value={multScanInterval}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multPartnerAgencyScanInterval: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Loneliness Decay & Open-Lover Loneliness Onset"
          description={`Days without player interaction before daily affection loneliness decay and open-partner loneliness desire begin (${multLonelinessDecay}x L = ${fmtDays(multLonelinessDecay)}).`}
        >
          <SliderInput
            value={multLonelinessDecay}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multLonelinessDecayStart: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Recent Attention & Guilt Window"
          description={`Window after spending time with the player where partners receive breakup boosts with side lovers, guilt confession eligibility, and neglect mitigation (${multRecentAttention}x L = ${fmtDays(multRecentAttention)}).`}
        >
          <SliderInput
            value={multRecentAttention}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multRecentAttentionWindow: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Very Recent Visit Window (Open Partners)"
          description={`Window after a player visit where open-relationship partners have reduced desire to seek a new side lover (${multVeryRecentVisit}x L = ${fmtDays(multVeryRecentVisit)}).`}
        >
          <SliderInput
            value={multVeryRecentVisit}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multVeryRecentVisitWindow: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Neglect Growth Onset (Troubled / Estranged)"
          description={`Days without player interaction before neglect temptation starts growing for Troubled/Estranged (<50 affection) partners (${multNeglectGrowth}x L = ${fmtDays(multNeglectGrowth)}).`}
        >
          <SliderInput
            value={multNeglectGrowth}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multNeglectGrowthStart: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Neglect Growth Onset (Content Partners)"
          description={`Days without player interaction before Content (50–74 affection) partners lose immunity and begin accumulating neglect temptation (${multContentNeglect}x L = ${fmtDays(multContentNeglect)}).`}
        >
          <SliderInput
            value={multContentNeglect}
            min={0.5}
            max={6.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multContentNeglectStart: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Prolonged Absence Penalty Onset (Affair Breakups)"
          description={`Days of player absence after which a cheating partner becomes less likely (-10%) to break off an ongoing secret affair (${multProlongedAbsence}x L = ${fmtDays(multProlongedAbsence)}).`}
        >
          <SliderInput
            value={multProlongedAbsence}
            min={0.5}
            max={6.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multProlongedAbsencePenalty: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Affair Talking -> Physical Escalation"
          description={`Duration of an unresolved Talking affair before it deterministically escalates to a Physical affair (${multTalkingEscalation}x L = ${fmtDays(multTalkingEscalation)}).`}
        >
          <SliderInput
            value={multTalkingEscalation}
            min={0.5}
            max={6.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multAffairTalkingEscalation: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Min Side-Lover Age Before Breakup"
          description={`Minimum duration a secret affair or open-relationship side fling must last before breakup rolls are evaluated (${multMinSideFlingAge}x L = ${fmtDays(multMinSideFlingAge)}).`}
        >
          <SliderInput
            value={multMinSideFlingAge}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multMinSideFlingAgeForBreakup: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Pending Force-Greet Intent TTL"
          description={`How long a queued partner conversation intent (SuggestOpen, SuggestClose, ConfessTalking, ConfessPhysical) waits for the player before expiring (${multIntentTTL}x L = ${fmtDays(multIntentTTL)}).`}
        >
          <SliderInput
            value={multIntentTTL}
            min={0.5}
            max={6.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multPartnerIntentTTL: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Home-Arrival Event Location Cooldown"
          description={`Minimum cooldown per home location between triggered side-lover arrival scenarios (${multHomeCooldown}x L = ${fmtDays(multHomeCooldown)}).`}
        >
          <SliderInput
            value={multHomeCooldown}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multHomeArrivalCooldown: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Affair Fatigue & Open Fling Novelty Fade Onset"
          description={`Duration of a secret affair or open fling before novelty fades and breakup likelihood increases with age (${multAffairFatigue}x L = ${fmtDays(multAffairFatigue)}).`}
        >
          <SliderInput
            value={multAffairFatigue}
            min={1.0}
            max={15.0}
            step={0.5}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multAffairFatigueStart: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Romantic Partner Attended Breakup Window"
          description={`Window after player interaction where a Romantic partner gets a 1.25x boost to breaking off a secret affair out of devotion (${multRomanticAttended}x L = ${fmtDays(multRomanticAttended)}).`}
        >
          <SliderInput
            value={multRomanticAttended}
            min={0.25}
            max={5.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multRomanticAttendedBreakup: Number(val.toFixed(2)) })}
          />
        </SettingRow>

        <SettingRow
          label="Suggest Open Relationship Neglect Onset"
          description={`Days without player interaction before neglect begins adding bonus probability to proposing an open relationship (${multSuggestOpenNeglect}x L = ${fmtDays(multSuggestOpenNeglect)}).`}
        >
          <SliderInput
            value={multSuggestOpenNeglect}
            min={0.5}
            max={6.0}
            step={0.25}
            unit="x L"
            onChange={(val) => updateMatchmakingSettings({ multSuggestOpenNeglectMin: Number(val.toFixed(2)) })}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}

