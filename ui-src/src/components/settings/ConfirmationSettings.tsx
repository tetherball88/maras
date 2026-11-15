import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SectionCard } from '../shared/SectionCard'
import { Users, HeartHandshake, UserMinus, Lock, Info } from 'lucide-react'

export const ConfirmationSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  const masterEnabled = settings.enableConfirmationModal ?? true
  const isMasterDisabled = !masterEnabled

  return (
    <div className="space-y-[1rem]">
      {/* Information Banner */}
      <div className="bg-panel-raised border border-border-divider rounded-md p-[0.75rem] text-xs text-foreground-secondary flex items-start gap-[0.5rem]">
        <Info className="w-[1.25em] h-[1.25em] text-accent flex-shrink-0 mt-[0.125rem]" />
        <div className="flex-1 space-y-[0.25rem]">
          <div className="font-semibold text-foreground-primary">
            In-Game Confirmation Modals
          </div>
          <div>
            Confirmation dialogs require confirmation before major relationship changes take effect.
            {settings.hasSkyrimNet ? (
              <span className="text-accent-bright block mt-[0.125rem]">
                SkyrimNet is detected: Modals will include a third &quot;Decline (Explain)&quot; button allowing freeform dialogue responses.
              </span>
            ) : (
              <span className="text-foreground-muted block mt-[0.125rem]">
                Modals display standard &quot;Accept&quot; and &quot;Cancel&quot; options.
              </span>
            )}
          </div>
        </div>
      </div>

      {/* Master Toggle & Participant Rules */}
      <SectionCard title="General Confirmation Rules" icon={Users}>
        <SettingRow
          label="Enable Confirmation Modals"
          description="Master toggle for all in-game confirmation popups. If disabled, all relationship transitions are auto-accepted without prompts."
        >
          <ToggleSwitch
            checked={masterEnabled}
            onChange={(val) => updateSetting('enableConfirmationModal', val)}
          />
        </SettingRow>

        <SettingRow
          label="Player-NPC Confirmations"
          description="Show confirmation modals when the player character is directly involved."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableConfirmPlayerNpc}
            onChange={(val) => updateSetting('enableConfirmPlayerNpc', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="NPC-NPC Confirmations"
          description="Show confirmation modals when interactions occur between two NPCs without player involvement."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableConfirmNpcNpc}
            onChange={(val) => updateSetting('enableConfirmNpcNpc', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Status Change Confirmations */}
      <SectionCard title="Status Change Confirmations" icon={HeartHandshake}>
        <SettingRow
          label="Confirm: Dating Proposals"
          description="Require confirmation modal when proposing or entering a dating relationship."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Dating}
            onChange={(val) => updateSetting('confirmStatusChange_Dating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Engagement Proposals"
          description="Require confirmation modal when proposing or accepting an engagement."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Engagement}
            onChange={(val) => updateSetting('confirmStatusChange_Engagement', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Lover Proposals"
          description="Require confirmation modal when proposing or becoming lovers."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Lover}
            onChange={(val) => updateSetting('confirmStatusChange_Lover', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Marriage Proposals"
          description="Require confirmation modal when proposing or accepting a marriage proposal."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Marry}
            onChange={(val) => updateSetting('confirmStatusChange_Marry', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Breakup & Divorce Confirmations */}
      <SectionCard title="Breakup & Divorce Confirmations" icon={UserMinus}>
        <SettingRow
          label="Confirm: Breakup Dating"
          description="Require confirmation modal when breaking up a dating relationship."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmBreakup_Dating}
            onChange={(val) => updateSetting('confirmBreakup_Dating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Breakup Engagement"
          description="Require confirmation modal when breaking off an engagement."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmBreakup_Engagement}
            onChange={(val) => updateSetting('confirmBreakup_Engagement', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Breakup Lover"
          description="Require confirmation modal when ending a lover relationship."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmBreakup_Lover}
            onChange={(val) => updateSetting('confirmBreakup_Lover', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Divorce Action"
          description="Require confirmation modal when an NPC or partner requests divorce."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmDivorce}
            onChange={(val) => updateSetting('confirmDivorce', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Low Affection Divorce"
          description="Require confirmation modal during low affection divorce questline."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmDivorceDuringLowAffectionQuest}
            onChange={(val) => updateSetting('confirmDivorceDuringLowAffectionQuest', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Open Relationship Confirmations */}
      <SectionCard title="Open Relationship Confirmations" icon={Lock}>
        <SettingRow
          label="Confirm: Open Relationship"
          description="Require confirmation modal when opening an existing relationship."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmOpenRelationship}
            onChange={(val) => updateSetting('confirmOpenRelationship', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Confirm: Close Relationship"
          description="Require confirmation modal when closing an open relationship."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmCloseRelationship}
            onChange={(val) => updateSetting('confirmCloseRelationship', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
