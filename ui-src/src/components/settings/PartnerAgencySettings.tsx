import React from 'react'
import { Heart, Sliders, Clock } from 'lucide-react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SectionCard } from '../shared/SectionCard'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'

export const PartnerAgencySettings: React.FC = () => {
  const { t, tp } = useTranslation()
  const matchmakingSettings = useMarasStore((s) => s.matchmakingSettings)
  const settings = useMarasStore((s) => s.settings)
  const updateMatchmakingSettings = useMarasStore((s) => s.updateMatchmakingSettings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const hasSkyrimNet = Boolean(settings.hasSkyrimNet) && !Boolean(settings.simulateNoSkyrimNet)
  const lonelinessDays =
    matchmakingSettings.lonelinessThresholdDays ?? settings.lonelinessThresholdDays ?? 2

  const fmtDays = (mult: number) => {
    const days = lonelinessDays * mult
    const formatted = Number.isInteger(days) ? String(days) : days.toFixed(1)
    return t('settings.partnerAgency.daysFormatted', { days: formatted })
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

  const unitMultiplier = t('settings.partnerAgency.unitMultiplier')
  const unitPercent = t('settings.partnerAgency.unitPercent')
  const unitDaysShort = t('settings.partnerAgency.unitDaysShort')
  const unitMultiplierL = t('settings.partnerAgency.unitMultiplierL')

  return (
    <div className="flex flex-col gap-[1rem]">
      {/* Section 1: Core Partner Agency Toggles */}
      <SectionCard
        title={t('settings.partnerAgency.engineTitle')}
        icon={Heart}
        subtitle={t('settings.partnerAgency.engineSubtitle')}
      >
        <SettingRow
          label={t('settings.partnerAgency.enableCheatingLabel')}
          description={t('settings.partnerAgency.enableCheatingDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingAgencyEnabled ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingAgencyEnabled: val })}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableOpenRelLabel')}
          description={t('settings.partnerAgency.enableOpenRelDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerOpenRelAgencyEnabled ?? true}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerOpenRelAgencyEnabled: val })}
          />
        </SettingRow>

        {hasSkyrimNet && (
          <SettingRow
            label={t('settings.partnerAgency.useAiStage2Label')}
            description={t('settings.partnerAgency.useAiStage2Desc')}
          >
            <ToggleSwitch
              checked={matchmakingSettings.playerPartnerCheatingUseLLM ?? true}
              onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingUseLLM: val })}
            />
          </SettingRow>
        )}

        <SettingRow
          label={t('settings.partnerAgency.confirmResultsLabel')}
          description={t('settings.partnerAgency.confirmResultsDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingConfirmResults ?? false}
            onChange={(val) => updateMatchmakingSettings({ playerPartnerCheatingConfirmResults: val })}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableForceGreetsLabel')}
          description={t('settings.partnerAgency.enableForceGreetsDesc', {
            days: fmtDays(multIntentTTL),
            mult: multIntentTTL,
          })}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerAgencyEnableForceGreets ?? true}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerAgencyEnableForceGreets: val })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableHomeArrivalLabel')}
          description={t('settings.partnerAgency.enableHomeArrivalDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerAgencyEnableHomeArrivalEvents ?? true}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerAgencyEnableHomeArrivalEvents: val })
            }
          />
        </SettingRow>
      </SectionCard>

      {/* Section 2: Temptation, Desire & Breakup Rules */}
      <SectionCard
        title={t('settings.partnerAgency.temptationTitle')}
        icon={Sliders}
        subtitle={t('settings.partnerAgency.temptationSubtitle')}
      >
        <SettingRow
          label={t('settings.partnerAgency.temptationMultLabel')}
          description={t('settings.partnerAgency.temptationMultDesc', {
            mult: (matchmakingSettings.playerPartnerCheatingChanceMultiplier ?? 1.0).toFixed(1),
          })}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerCheatingChanceMultiplier ?? 1.0}
            min={0.1}
            max={3.0}
            step={0.1}
            unit={unitMultiplier}
            onChange={(val) =>
              updateMatchmakingSettings({
                playerPartnerCheatingChanceMultiplier: Number(val.toFixed(1)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.openDesireMultLabel')}
          description={t('settings.partnerAgency.openDesireMultDesc', {
            mult: (matchmakingSettings.playerPartnerAgencyOpenLoverChanceMultiplier ?? 1.0).toFixed(
              1
            ),
          })}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerAgencyOpenLoverChanceMultiplier ?? 1.0}
            min={0.1}
            max={3.0}
            step={0.1}
            unit={unitMultiplier}
            onChange={(val) =>
              updateMatchmakingSettings({
                playerPartnerAgencyOpenLoverChanceMultiplier: Number(val.toFixed(1)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.breakupMultLabel')}
          description={t('settings.partnerAgency.breakupMultDesc', {
            days: fmtDays(multMinSideFlingAge),
            mult: (matchmakingSettings.playerPartnerAgencyBreakupChanceMultiplier ?? 1.0).toFixed(
              1
            ),
          })}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerAgencyBreakupChanceMultiplier ?? 1.0}
            min={0.1}
            max={3.0}
            step={0.1}
            unit={unitMultiplier}
            onChange={(val) =>
              updateMatchmakingSettings({
                playerPartnerAgencyBreakupChanceMultiplier: Number(val.toFixed(1)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.maxRollCapLabel')}
          description={t('settings.partnerAgency.maxRollCapDesc', {
            cap: matchmakingSettings.playerPartnerCheatingMaxRollCap ?? 65,
          })}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerCheatingMaxRollCap ?? 65}
            min={5}
            max={100}
            step={5}
            unit={unitPercent}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerCheatingMaxRollCap: Math.round(val) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.maxSecretLoversLabel')}
          description={t('settings.partnerAgency.maxSecretLoversDesc', {
            count: matchmakingSettings.playerPartnerCheatingMaxSecretLovers ?? 1,
          })}
        >
          <SliderInput
            value={matchmakingSettings.playerPartnerCheatingMaxSecretLovers ?? 1}
            min={1}
            max={3}
            step={1}
            unit=""
            onChange={(val) =>
              updateMatchmakingSettings({
                playerPartnerCheatingMaxSecretLovers: Math.round(val),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.allowWhenHappyLabel')}
          description={t('settings.partnerAgency.allowWhenHappyDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingAllowWhenHappy ?? false}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerCheatingAllowWhenHappy: val })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableNeglectLabel')}
          description={t('settings.partnerAgency.enableNeglectDesc', {
            troubledDays: fmtDays(multNeglectGrowth),
            troubledMult: multNeglectGrowth,
            contentDays: fmtDays(multContentNeglect),
            contentMult: multContentNeglect,
          })}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableNeglect ?? true}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerCheatingEnableNeglect: val })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableRevengeLabel')}
          description={t('settings.partnerAgency.enableRevengeDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableRevenge ?? true}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerCheatingEnableRevenge: val })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableHierarchyLabel')}
          description={t('settings.partnerAgency.enableHierarchyDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableHierarchy ?? true}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerCheatingEnableHierarchy: val })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.enableChildrenAnchorLabel')}
          description={t('settings.partnerAgency.enableChildrenAnchorDesc')}
        >
          <ToggleSwitch
            checked={matchmakingSettings.playerPartnerCheatingEnableChildrenAnchor ?? true}
            onChange={(val) =>
              updateMatchmakingSettings({ playerPartnerCheatingEnableChildrenAnchor: val })
            }
          />
        </SettingRow>
      </SectionCard>

      {/* Section 3: Loneliness & Partner Agency Day-Threshold Multipliers */}
      <SectionCard
        title={t('settings.partnerAgency.multipliersTitle')}
        icon={Clock}
        subtitle={t('settings.partnerAgency.multipliersSubtitle', { days: fmtDays(1) })}
      >
        <SettingRow
          label={t('settings.partnerAgency.baseLonelinessLabel')}
          description={tp('settings.partnerAgency.baseLonelinessDesc', lonelinessDays)}
        >
          <SliderInput
            value={lonelinessDays}
            min={1}
            max={14}
            step={1}
            unit={unitDaysShort}
            onChange={(val) => {
              const days = Math.round(val)
              updateSetting('lonelinessThresholdDays', days)
              updateMatchmakingSettings({ lonelinessThresholdDays: days })
            }}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.scanIntervalLabel')}
          description={t('settings.partnerAgency.scanIntervalDesc', {
            mult: multScanInterval,
            days: fmtDays(multScanInterval),
          })}
        >
          <SliderInput
            value={multScanInterval}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({
                multPartnerAgencyScanInterval: Number(val.toFixed(2)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.lonelinessDecayLabel')}
          description={t('settings.partnerAgency.lonelinessDecayDesc', {
            mult: multLonelinessDecay,
            days: fmtDays(multLonelinessDecay),
          })}
        >
          <SliderInput
            value={multLonelinessDecay}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multLonelinessDecayStart: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.recentAttentionLabel')}
          description={t('settings.partnerAgency.recentAttentionDesc', {
            mult: multRecentAttention,
            days: fmtDays(multRecentAttention),
          })}
        >
          <SliderInput
            value={multRecentAttention}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multRecentAttentionWindow: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.veryRecentVisitLabel')}
          description={t('settings.partnerAgency.veryRecentVisitDesc', {
            mult: multVeryRecentVisit,
            days: fmtDays(multVeryRecentVisit),
          })}
        >
          <SliderInput
            value={multVeryRecentVisit}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multVeryRecentVisitWindow: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.neglectTroubledLabel')}
          description={t('settings.partnerAgency.neglectTroubledDesc', {
            mult: multNeglectGrowth,
            days: fmtDays(multNeglectGrowth),
          })}
        >
          <SliderInput
            value={multNeglectGrowth}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multNeglectGrowthStart: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.neglectContentLabel')}
          description={t('settings.partnerAgency.neglectContentDesc', {
            mult: multContentNeglect,
            days: fmtDays(multContentNeglect),
          })}
        >
          <SliderInput
            value={multContentNeglect}
            min={0.5}
            max={6.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multContentNeglectStart: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.prolongedAbsenceLabel')}
          description={t('settings.partnerAgency.prolongedAbsenceDesc', {
            mult: multProlongedAbsence,
            days: fmtDays(multProlongedAbsence),
          })}
        >
          <SliderInput
            value={multProlongedAbsence}
            min={0.5}
            max={6.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({
                multProlongedAbsencePenalty: Number(val.toFixed(2)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.talkingEscalationLabel')}
          description={t('settings.partnerAgency.talkingEscalationDesc', {
            mult: multTalkingEscalation,
            days: fmtDays(multTalkingEscalation),
          })}
        >
          <SliderInput
            value={multTalkingEscalation}
            min={0.5}
            max={6.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({
                multAffairTalkingEscalation: Number(val.toFixed(2)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.minSideFlingAgeLabel')}
          description={t('settings.partnerAgency.minSideFlingAgeDesc', {
            mult: multMinSideFlingAge,
            days: fmtDays(multMinSideFlingAge),
          })}
        >
          <SliderInput
            value={multMinSideFlingAge}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({
                multMinSideFlingAgeForBreakup: Number(val.toFixed(2)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.intentTtlLabel')}
          description={t('settings.partnerAgency.intentTtlDesc', {
            mult: multIntentTTL,
            days: fmtDays(multIntentTTL),
          })}
        >
          <SliderInput
            value={multIntentTTL}
            min={0.5}
            max={6.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multPartnerIntentTTL: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.homeCooldownLabel')}
          description={t('settings.partnerAgency.homeCooldownDesc', {
            mult: multHomeCooldown,
            days: fmtDays(multHomeCooldown),
          })}
        >
          <SliderInput
            value={multHomeCooldown}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multHomeArrivalCooldown: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.affairFatigueLabel')}
          description={t('settings.partnerAgency.affairFatigueDesc', {
            mult: multAffairFatigue,
            days: fmtDays(multAffairFatigue),
          })}
        >
          <SliderInput
            value={multAffairFatigue}
            min={1.0}
            max={15.0}
            step={0.5}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({ multAffairFatigueStart: Number(val.toFixed(2)) })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.romanticAttendedLabel')}
          description={t('settings.partnerAgency.romanticAttendedDesc', {
            mult: multRomanticAttended,
            days: fmtDays(multRomanticAttended),
          })}
        >
          <SliderInput
            value={multRomanticAttended}
            min={0.25}
            max={5.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({
                multRomanticAttendedBreakup: Number(val.toFixed(2)),
              })
            }
          />
        </SettingRow>

        <SettingRow
          label={t('settings.partnerAgency.suggestOpenNeglectLabel')}
          description={t('settings.partnerAgency.suggestOpenNeglectDesc', {
            mult: multSuggestOpenNeglect,
            days: fmtDays(multSuggestOpenNeglect),
          })}
        >
          <SliderInput
            value={multSuggestOpenNeglect}
            min={0.5}
            max={6.0}
            step={0.25}
            unit={unitMultiplierL}
            onChange={(val) =>
              updateMatchmakingSettings({
                multSuggestOpenNeglectMin: Number(val.toFixed(2)),
              })
            }
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
