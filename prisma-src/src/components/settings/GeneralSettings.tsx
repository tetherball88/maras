import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Sliders, Monitor } from 'lucide-react'

export const GeneralSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const uiScale = settings.uiScale ?? 1.0

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="Interface & Accessibility" icon={Monitor}>
        <SettingRow
          label="Auto-Pause Game on Dashboard Open"
          description="Automatically pause Skyrim whenever the M.A.R.A.S dashboard is opened. You can also pause or unpause at any time using the Pause button in the top-right header bar."
        >
          <ToggleSwitch
            checked={Boolean(settings.autoPauseOnOpen)}
            onChange={(val) => updateSetting('autoPauseOnOpen', val)}
          />
        </SettingRow>

        <SettingRow
          label="Full Screen Mode"
          description="Open the M.A.R.A.S dashboard in full-screen mode instead of a centered window."
        >
          <ToggleSwitch
            checked={Boolean(settings.uiFullScreen)}
            onChange={(val) => updateSetting('uiFullScreen', val)}
          />
        </SettingRow>

        <SettingRow
          label="UI & Font Scale (Accessibility)"
          description={`Scale all fonts, icons, cards, and UI elements proportionally across the dashboard and modals (${Math.round(uiScale * 100)}%).`}
        >
          <SliderInput
            value={uiScale}
            min={0.75}
            max={1.5}
            step={0.05}
            unit="x"
            onChange={(val) => updateSetting('uiScale', val)}
          />
        </SettingRow>
      </SectionCard>

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
          description={`Scale daily affection decay rate when not interacting (${Number(settings.affectionDecayMult.toFixed(2))}x).`}
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
          label="Disable Polygamy Quest"
          description="Prevent 'The Flame Divides Not' quest from starting. If currently running, stops and resets the quest."
        >
          <ToggleSwitch
            checked={!!settings.disablePolygamyQuest}
            onChange={(val) => updateSetting('disablePolygamyQuest', val)}
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
