import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { SelectInput } from '../shared/SelectInput'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Bot } from 'lucide-react'

export const AIDialogueSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  const genderOptions = [
    { value: 0, label: t('settings.aiDialogue.genderOpposite') },
    { value: 1, label: t('settings.aiDialogue.genderSame') },
    { value: 2, label: t('settings.aiDialogue.genderAll') },
  ]

  return (
    <div className="space-y-[1rem]">
      <SectionCard title={t('settings.aiDialogue.title')} icon={Bot}>
        <SettingRow
          label={t('settings.aiDialogue.targetGenderLabel')}
          description={t('settings.aiDialogue.targetGenderDesc')}
        >
          <SelectInput
            value={settings.startDialGender}
            options={genderOptions}
            onChange={(val) => updateSetting('startDialGender', parseInt(val))}
            className="min-w-[12rem] w-auto"
          />
        </SettingRow>

        <SettingRow
          label={t('settings.aiDialogue.minRelRankLabel')}
          description={t('settings.aiDialogue.minRelRankDesc', {
            rank: settings.minRelRankForDial,
          })}
        >
          <SliderInput
            value={settings.minRelRankForDial}
            min={-4}
            max={4}
            step={1}
            onChange={(val) => updateSetting('minRelRankForDial', val)}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
