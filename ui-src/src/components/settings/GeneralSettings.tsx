import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { reloadLocalization } from '../../store/uiBridge'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SelectInput } from '../shared/SelectInput'
import { SectionCard } from '../shared/SectionCard'
import { Button } from '../ui/button'
import { Sliders, Monitor } from 'lucide-react'

export const GeneralSettings: React.FC = () => {
  const { t, tp, selectedLanguage, detectedSkyrimLanguage, availableLanguages } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const uiScale = settings.uiScale ?? 1.0
  const updateMatchmakingSettings = useMarasStore((s) => s.updateMatchmakingSettings)
  const lonelinessDays = settings.lonelinessThresholdDays ?? 2

  const languageOptions = [
    {
      value: 'auto',
      label: t('settings.general.languageAuto', {
        lang: (detectedSkyrimLanguage || 'en').toUpperCase(),
      }),
    },
    ...availableLanguages.map((m) => ({
      value: m.code,
      label:
        m.nativeName && m.nativeName !== m.name
          ? `${m.nativeName} (${m.name})`
          : (m.nativeName || m.name || m.code),
    })),
  ]

  return (
    <div className="space-y-[1rem]">
      <SectionCard title={t('settings.general.interfaceTitle')} icon={Monitor}>
        <SettingRow
          label={t('settings.general.languageLabel')}
          description={t('settings.general.languageDesc')}
        >
          <SelectInput
            className="min-w-[13rem] w-auto"
            value={settings.language || selectedLanguage || 'auto'}
            options={languageOptions}
            onChange={(val) => updateSetting('language', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.reloadLocalizationLabel')}
          description={t('settings.general.reloadLocalizationDesc')}
        >
          <Button variant="secondary" size="sm" onClick={() => reloadLocalization()}>
            {t('settings.general.reloadLocalizationBtn')}
          </Button>
        </SettingRow>

        <SettingRow
          label={t('settings.general.autoPauseLabel')}
          description={t('settings.general.autoPauseDesc')}
        >
          <ToggleSwitch
            checked={Boolean(settings.autoPauseOnOpen)}
            onChange={(val) => updateSetting('autoPauseOnOpen', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.fullScreenLabel')}
          description={t('settings.general.fullScreenDesc')}
        >
          <ToggleSwitch
            checked={Boolean(settings.uiFullScreen)}
            onChange={(val) => updateSetting('uiFullScreen', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.uiScaleLabel')}
          description={t('settings.general.uiScaleDesc', {
            percent: Math.round(uiScale * 100),
          })}
        >
          <SliderInput
            value={uiScale}
            min={0.75}
            max={1.5}
            step={0.05}
            unit={t('settings.general.unitMultiplier')}
            onChange={(val) => updateSetting('uiScale', val)}
          />
        </SettingRow>
      </SectionCard>

      <SectionCard title={t('settings.general.modSettingsTitle')} icon={Sliders}>
        <SettingRow
          label={t('settings.general.skipWeddingLabel')}
          description={t('settings.general.skipWeddingDesc')}
        >
          <ToggleSwitch
            checked={settings.skipWedding}
            onChange={(val) => updateSetting('skipWedding', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.affectionDecayLabel')}
          description={t('settings.general.affectionDecayDesc', {
            mult: Number(settings.affectionDecayMult.toFixed(2)),
          })}
        >
          <SliderInput
            value={settings.affectionDecayMult}
            min={0.0}
            max={2.0}
            step={0.1}
            unit={t('settings.general.unitMultiplier')}
            onChange={(val) => updateSetting('affectionDecayMult', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.lonelinessThresholdLabel')}
          description={t('settings.general.lonelinessThresholdDesc', {
            days: lonelinessDays,
            scanDays: Math.max(1, Math.round(lonelinessDays * 1.5)),
            escalationDays: Math.round(lonelinessDays * 2),
          })}
        >
          <SliderInput
            value={lonelinessDays}
            min={1}
            max={14}
            step={1}
            unit={t('settings.general.unitDaysShort')}
            onChange={(val) => {
              const days = Math.round(val)
              updateSetting('lonelinessThresholdDays', days)
              updateMatchmakingSettings({ lonelinessThresholdDays: days })
            }}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.enablePolygamyLabel')}
          description={t('settings.general.enablePolygamyDesc')}
        >
          <ToggleSwitch
            checked={settings.enablePolygamy}
            onChange={(val) => updateSetting('enablePolygamy', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.disablePolygamyQuestLabel')}
          description={t('settings.general.disablePolygamyQuestDesc')}
        >
          <ToggleSwitch
            checked={!!settings.disablePolygamyQuest}
            onChange={(val) => updateSetting('disablePolygamyQuest', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.general.hotkeyModifiersLabel')}
          description={t('settings.general.hotkeyModifiersDesc')}
        >
          <div className="flex items-center gap-[0.75rem] text-xs text-foreground-primary">
            <label className="flex items-center gap-[0.25rem] cursor-pointer">
              <input
                type="checkbox"
                checked={settings.toggleUIModCtrl}
                onChange={(e) => updateSetting('toggleUIModCtrl', e.target.checked)}
                className="accent-accent"
              />
              <span>{t('settings.general.modCtrl')}</span>
            </label>
            <label className="flex items-center gap-[0.25rem] cursor-pointer">
              <input
                type="checkbox"
                checked={settings.toggleUIModShift}
                onChange={(e) => updateSetting('toggleUIModShift', e.target.checked)}
                className="accent-accent"
              />
              <span>{t('settings.general.modShift')}</span>
            </label>
            <label className="flex items-center gap-[0.25rem] cursor-pointer">
              <input
                type="checkbox"
                checked={settings.toggleUIModAlt}
                onChange={(e) => updateSetting('toggleUIModAlt', e.target.checked)}
                className="accent-accent"
              />
              <span>{t('settings.general.modAlt')}</span>
            </label>
          </div>
        </SettingRow>
      </SectionCard>
    </div>
  )
}
