import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Sliders } from 'lucide-react'

export const GeneralSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="General Mod Settings" icon={Sliders}>
        <SettingRow
          label="Skip Wedding Ceremony"
          description="Immediately complete marriages without scheduling a ceremony at the Temple of Mara."
        >
          <ToggleSwitch
            checked={settings.skipWedding}
            onChange={(val) => updateSetting('skipWedding', val)}
          />
        </SettingRow>

        <SettingRow
          label="Affection Decay Multiplier"
          description={`Scale daily affection decay rate when not interacting (${settings.affectionDecayMult}x).`}
        >
          <SliderInput
            value={settings.affectionDecayMult}
            min={0.0}
            max={2.0}
            step={0.1}
            unit="x"
            onChange={(val) => updateSetting('affectionDecayMult', val)}
          />
        </SettingRow>

        <SettingRow
          label="Enable Polygamy"
          description="Allow marrying multiple spouses and establishing rank hierarchies."
        >
          <ToggleSwitch
            checked={settings.enablePolygamy}
            onChange={(val) => updateSetting('enablePolygamy', val)}
          />
        </SettingRow>

        <SettingRow
          label="Toggle Dashboard Hotkey Modifiers"
          description="Require Ctrl / Shift / Alt keys in combination with your configured toggle key."
        >
          <div className="flex items-center gap-[0.75rem] text-xs text-foreground-primary">
            <label className="flex items-center gap-[0.25rem] cursor-pointer">
              <input
                type="checkbox"
                checked={settings.toggleUIModCtrl}
                onChange={(e) => updateSetting('toggleUIModCtrl', e.target.checked)}
                className="accent-accent"
              />
              <span>Ctrl</span>
            </label>
            <label className="flex items-center gap-[0.25rem] cursor-pointer">
              <input
                type="checkbox"
                checked={settings.toggleUIModShift}
                onChange={(e) => updateSetting('toggleUIModShift', e.target.checked)}
                className="accent-accent"
              />
              <span>Shift</span>
            </label>
            <label className="flex items-center gap-[0.25rem] cursor-pointer">
              <input
                type="checkbox"
                checked={settings.toggleUIModAlt}
                onChange={(e) => updateSetting('toggleUIModAlt', e.target.checked)}
                className="accent-accent"
              />
              <span>Alt</span>
            </label>
          </div>
        </SettingRow>
      </SectionCard>
    </div>
  )
}
