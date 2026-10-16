import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SectionCard } from '../shared/SectionCard'
import { Baby } from 'lucide-react'

export const ChildrenSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="Children & Household Pet Settings" icon={Baby}>
        <SettingRow
          label="Global Ban Critters"
          description="Globally disable kid critter adoption events (foxes, hares, mudcrabs) for all children."
        >
          <ToggleSwitch
            checked={!!settings.globalBanCritter}
            onChange={(val) => updateSetting('globalBanCritter' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label="Global Ban Pets"
          description="Globally disable household pet adoption events (dogs) for all children."
        >
          <ToggleSwitch
            checked={!!settings.globalBanPet}
            onChange={(val) => updateSetting('globalBanPet' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label="Prioritize Biological Parent"
          description="When divorcing, if only one partner is the biological parent, award custody to them (overrides gender and player rules)."
        >
          <ToggleSwitch
            checked={settings.childCustodyPrioritizeBioParent !== false}
            onChange={(val) => updateSetting('childCustodyPrioritizeBioParent' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label="Gender Custody Policy"
          description="Determines which parent retains child custody upon divorce when biological rules do not decide."
        >
          <select
            value={settings.childCustodyGenderPolicy ?? 0}
            onChange={(e) => updateSetting('childCustodyGenderPolicy' as any, parseInt(e.target.value, 10))}
            className="bg-panel-inset text-foreground-primary text-xs border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus:outline-none focus:border-border-accent"
          >
            <option value={0}>Stay with Mother (Default)</option>
            <option value={1}>Stay with Father</option>
          </select>
        </SettingRow>

        <SettingRow
          label="Player Custody Override"
          description="Overrides gender custody in divorces involving the Player."
        >
          <select
            value={settings.childCustodyPlayerPolicy ?? 0}
            onChange={(e) => updateSetting('childCustodyPlayerPolicy' as any, parseInt(e.target.value, 10))}
            className="bg-panel-inset text-foreground-primary text-xs border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus:outline-none focus:border-border-accent"
          >
            <option value={0}>Use Gender Custody (Default)</option>
            <option value={1}>Always with Player</option>
            <option value={2}>Never with Player</option>
          </select>
        </SettingRow>
      </SectionCard>
    </div>
  )
}
