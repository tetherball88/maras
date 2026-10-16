import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { SelectInput } from '../shared/SelectInput'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Bot } from 'lucide-react'

export const AIDialogueSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  const genderOptions = [
    { value: 0, label: 'Opposite Gender Only' },
    { value: 1, label: 'Same Gender Only' },
    { value: 2, label: 'All Genders' },
  ]

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="AI Dialogue & Interactions" icon={Bot}>
        <SettingRow
          label="Allowed Dialogue Target Gender"
          description="Filter which NPCs can initiate dynamic romance dialogues."
        >
          <SelectInput
            value={settings.startDialGender}
            options={genderOptions}
            onChange={(val) => updateSetting('startDialGender', parseInt(val))}
          />
        </SettingRow>

        <SettingRow
          label="Minimum Relationship Rank for Dialogue"
          description={`Required RelationshipRank integer (-4 to 4) for dialogue initiation (${settings.minRelRankForDial}).`}
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
