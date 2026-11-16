import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SectionCard } from '../shared/SectionCard'
import { Users, HeartHandshake, UserMinus, Lock, Info } from 'lucide-react'

export const ConfirmationSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  void tp
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
            {t('settings.confirmations.bannerTitle')}
          </div>
          <div>
            {t('settings.confirmations.bannerDesc')}
            {settings.hasSkyrimNet ? (
              <span className="text-accent-bright block mt-[0.125rem]">
                {t('settings.confirmations.bannerSkyrimNet')}
              </span>
            ) : (
              <span className="text-foreground-muted block mt-[0.125rem]">
                {t('settings.confirmations.bannerStandard')}
              </span>
            )}
          </div>
        </div>
      </div>

      {/* Master Toggle & Participant Rules */}
      <SectionCard title={t('settings.confirmations.generalTitle')} icon={Users}>
        <SettingRow
          label={t('settings.confirmations.enableMasterLabel')}
          description={t('settings.confirmations.enableMasterDesc')}
        >
          <ToggleSwitch
            checked={masterEnabled}
            onChange={(val) => updateSetting('enableConfirmationModal', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.playerNpcLabel')}
          description={t('settings.confirmations.playerNpcDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableConfirmPlayerNpc}
            onChange={(val) => updateSetting('enableConfirmPlayerNpc', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.npcNpcLabel')}
          description={t('settings.confirmations.npcNpcDesc')}
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
      <SectionCard title={t('settings.confirmations.statusChangeTitle')} icon={HeartHandshake}>
        <SettingRow
          label={t('settings.confirmations.confirmDatingLabel')}
          description={t('settings.confirmations.confirmDatingDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Dating}
            onChange={(val) => updateSetting('confirmStatusChange_Dating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.confirmEngagementLabel')}
          description={t('settings.confirmations.confirmEngagementDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Engagement}
            onChange={(val) => updateSetting('confirmStatusChange_Engagement', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.confirmLoverLabel')}
          description={t('settings.confirmations.confirmLoverDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmStatusChange_Lover}
            onChange={(val) => updateSetting('confirmStatusChange_Lover', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.confirmMarryLabel')}
          description={t('settings.confirmations.confirmMarryDesc')}
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
      <SectionCard title={t('settings.confirmations.breakupTitle')} icon={UserMinus}>
        <SettingRow
          label={t('settings.confirmations.breakupDatingLabel')}
          description={t('settings.confirmations.breakupDatingDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmBreakup_Dating}
            onChange={(val) => updateSetting('confirmBreakup_Dating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.breakupEngagementLabel')}
          description={t('settings.confirmations.breakupEngagementDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmBreakup_Engagement}
            onChange={(val) => updateSetting('confirmBreakup_Engagement', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.breakupLoverLabel')}
          description={t('settings.confirmations.breakupLoverDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmBreakup_Lover}
            onChange={(val) => updateSetting('confirmBreakup_Lover', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.divorceLabel')}
          description={t('settings.confirmations.divorceDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmDivorce}
            onChange={(val) => updateSetting('confirmDivorce', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.lowAffectionDivorceLabel')}
          description={t('settings.confirmations.lowAffectionDivorceDesc')}
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
      <SectionCard title={t('settings.confirmations.openRelTitle')} icon={Lock}>
        <SettingRow
          label={t('settings.confirmations.openRelLabel')}
          description={t('settings.confirmations.openRelDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.confirmOpenRelationship}
            onChange={(val) => updateSetting('confirmOpenRelationship', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.confirmations.closeRelLabel')}
          description={t('settings.confirmations.closeRelDesc')}
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
