import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SectionCard } from '../shared/SectionCard'
import { CheckCircle, AlertTriangle, Target } from 'lucide-react'

export const SkyrimNetSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  const isGatedDisabled = !settings.hasSkyrimNet

  const toggleSimSlot = (
    <label className="flex items-center gap-[0.375rem] text-xs text-foreground-secondary cursor-pointer">
      <span>Simulate Non-SkyrimNet:</span>
      <ToggleSwitch
        checked={Boolean(settings.simulateNoSkyrimNet)}
        onChange={(val) => updateSetting('simulateNoSkyrimNet', val)}
      />
    </label>
  )

  return (
    <div className="space-y-[1rem]">
      {isGatedDisabled ? (
        <div className="bg-status-warning-soft border border-status-warning rounded-md p-[0.875rem] text-xs text-foreground-primary flex items-start gap-[0.5rem]">
          <AlertTriangle className="w-[1.25em] h-[1.25em] text-status-warning flex-shrink-0 mt-[0.125rem]" />
          <div className="flex-1">
            <div className="font-semibold text-status-warning mb-[0.125rem]">
              {settings.simulateNoSkyrimNet ? 'SkyrimNet Disabled (Simulation Mode)' : 'SkyrimNet.dll Not Detected'}
            </div>
            <div>
              {settings.simulateNoSkyrimNet
                ? 'Non-SkyrimNet simulation is currently active. M.A.R.A.S features operate under standard non-AI rules (Dice Roll matchmaking, vanilla proposal dialog, 2-button modals). You can disable simulation in Cheats & Debug Settings.'
                : 'SkyrimNet is required for AI-generated dialogue, prompt actions, and response confirmation modals. Features in this section are automatically disabled per mod safety gating rules.'}
            </div>
          </div>
          {settings.simulateNoSkyrimNet && (
            <button
              type="button"
              onClick={() => updateSetting('simulateNoSkyrimNet', false)}
              className="px-2 py-1 text-xs font-medium rounded border border-status-warning/40 text-status-warning hover:bg-status-warning/20 transition-all cursor-pointer whitespace-nowrap"
            >
              Disable Simulation
            </button>
          )}
        </div>
      ) : (
        <div className="bg-status-success-soft border border-status-success rounded-md p-[0.625rem] text-xs text-foreground-primary flex items-center gap-[0.375rem]">
          <CheckCircle className="w-[1.125em] h-[1.125em] text-status-success" />
          <span>SkyrimNet DLL Active — Prompt extensions & event listeners registered.</span>
        </div>
      )}

      <SectionCard title="AI Relationship Action Detection" icon={Target} actionSlot={toggleSimSlot}>
        <SettingRow
          label="Action Distance Radius (units)"
          description="Maximum distance in game units (~70 units ≈ 1 meter) to scan for valid partner targets. If no eligible target actor is within this radius, proposal and consideration actions remain unavailable."
          disabled={isGatedDisabled}
        >
          <input
            type="number"
            min={100}
            max={20000}
            step={100}
            value={settings.skyrimnetActionRadius ?? 3000}
            onChange={(e) => {
              const val = parseFloat(e.target.value)
              if (!isNaN(val) && val > 0) {
                updateSetting('skyrimnetActionRadius', val)
              }
            }}
            disabled={isGatedDisabled}
            className="w-28 px-2 py-1 bg-surface-secondary border border-border-light rounded text-sm text-foreground-primary focus:outline-none focus:border-primary disabled:opacity-50 text-right"
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}

