import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Baby, GraduationCap } from 'lucide-react'

export const ChildrenSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const trainingDays = settings.childTrainingDurationDays ?? 14
  const trainingCost = settings.childTrainingCost ?? 2000

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="Child Training & Adulthood" icon={GraduationCap}>
        <SettingRow
          label="Training Duration (Days)"
          description={`Number of in-game days required for a child in Stage 3 (Training) to graduate to Stage 4 (Adult) (${trainingDays} day${trainingDays === 1 ? '' : 's'}). You can also complete training early from the child's profile.`}
        >
          <SliderInput
            value={trainingDays}
            min={1}
            max={120}
            step={1}
            unit="d"
            onChange={(val) => updateSetting('childTrainingDurationDays', Math.round(val))}
          />
        </SettingRow>

        <SettingRow
          label="Training Tuition Cost (Gold)"
          description={`Gold required to send a player child away for adolescent training (${trainingCost} gold). Set to 0 for free training.`}
        >
          <SliderInput
            value={trainingCost}
            min={0}
            max={10000}
            step={100}
            unit="g"
            onChange={(val) => updateSetting('childTrainingCost', Math.round(val))}
          />
        </SettingRow>

        <SettingRow
          label="Require Faction Membership for Guild Training"
          description="When enabled, sending a player child to guild, temple, or Daedric covenant training requires the Player to meet that location's membership or patronage criteria."
        >
          <ToggleSwitch
            checked={settings.childTrainingRequireFaction ?? true}
            onChange={(val) => updateSetting('childTrainingRequireFaction', val)}
          />
        </SettingRow>
      </SectionCard>

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
