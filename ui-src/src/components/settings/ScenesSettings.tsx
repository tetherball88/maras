import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Clapperboard, HeartHandshake, Flame, Info, Building, Home, MapPin } from 'lucide-react'

export const ScenesSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  const masterEnabled = settings.enableScenes ?? true
  const isMasterDisabled = !masterEnabled

  return (
    <div className="space-y-[1rem]">
      {/* Information Banner */}
      <div className="bg-panel-raised border border-border-divider rounded-md p-[0.75rem] text-xs text-foreground-secondary flex items-start gap-[0.5rem]">
        <Info className="w-[1.25em] h-[1.25em] text-accent flex-shrink-0 mt-[0.125rem]" />
        <div className="flex-1 space-y-[0.25rem]">
          <div className="font-semibold text-foreground-primary">
            {t('settings.scenes.bannerTitle')}
          </div>
          <div>{t('settings.scenes.bannerDesc')}</div>
        </div>
      </div>

      {/* Master Toggle & General Rules */}
      <SectionCard title={t('settings.scenes.generalTitle')} icon={Clapperboard}>
        <SettingRow
          label={t('settings.scenes.enableMasterLabel')}
          description={t('settings.scenes.enableMasterDesc')}
        >
          <ToggleSwitch
            checked={masterEnabled}
            onChange={(val) => updateSetting('enableScenes', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.cooldownLabel')}
          description={t('settings.scenes.cooldownDesc')}
          disabled={isMasterDisabled}
        >
          <SliderInput
            value={settings.scenesCooldownHours ?? 24}
            min={0}
            max={168}
            step={1}
            unit={t('settings.scenes.unitHours')}
            disabled={isMasterDisabled}
            onChange={(val) => updateSetting('scenesCooldownHours', val)}
          />
        </SettingRow>
      </SectionCard>

      {/* Standard Relationship Scenes */}
      <SectionCard title={t('settings.scenes.relationshipTitle')} icon={HeartHandshake}>
        <SettingRow
          label={t('settings.scenes.datingLabel')}
          description={t('settings.scenes.datingDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Dating ?? true}
            onChange={(val) => updateSetting('enableScenes_Dating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.loverLabel')}
          description={t('settings.scenes.loverDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Lover ?? true}
            onChange={(val) => updateSetting('enableScenes_Lover', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.engagedLabel')}
          description={t('settings.scenes.engagedDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Engaged ?? true}
            onChange={(val) => updateSetting('enableScenes_Engaged', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.marriedLabel')}
          description={t('settings.scenes.marriedDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Married ?? true}
            onChange={(val) => updateSetting('enableScenes_Married', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Complex & Illicit Scenes */}
      <SectionCard title={t('settings.scenes.complexTitle')} icon={Flame}>
        <SettingRow
          label={t('settings.scenes.openRelLabel')}
          description={t('settings.scenes.openRelDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_OpenRelationship ?? true}
            onChange={(val) => updateSetting('enableScenes_OpenRelationship', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.cheatingSecretLabel')}
          description={t('settings.scenes.cheatingSecretDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Cheating ?? true}
            onChange={(val) => updateSetting('enableScenes_Cheating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.cheatingDiscoveredLabel')}
          description={t('settings.scenes.cheatingDiscoveredDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_CheatingDiscovered ?? true}
            onChange={(val) => updateSetting('enableScenes_CheatingDiscovered', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.cheatingCaughtLabel')}
          description={t('settings.scenes.cheatingCaughtDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_CheatingCaught ?? true}
            onChange={(val) => updateSetting('enableScenes_CheatingCaught', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Allowed Locations: Interiors */}
      <SectionCard title={t('settings.scenes.interiorsTitle')} icon={Building}>
        <SettingRow
          label={t('settings.scenes.locTempleLabel')}
          description={t('settings.scenes.locTempleDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeTemple ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeTemple', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locStoreLabel')}
          description={t('settings.scenes.locStoreDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeStore ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeStore', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locStewardLabel')}
          description={t('settings.scenes.locStewardDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeStewardsDwelling ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeStewardsDwelling', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locInnLabel')}
          description={t('settings.scenes.locInnDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeInn ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeInn', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locHouseLabel')}
          description={t('settings.scenes.locHouseDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeHouse ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeHouse', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locBarracksLabel')}
          description={t('settings.scenes.locBarracksDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeBarracks ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeBarracks', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locGuildLabel')}
          description={t('settings.scenes.locGuildDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeGuild ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeGuild', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Allowed Locations: Player Houses */}
      <SectionCard title={t('settings.scenes.playerHousesTitle')} icon={Home}>
        <SettingRow
          label={t('settings.scenes.locPlayerHouseLabel')}
          description={t('settings.scenes.locPlayerHouseDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypePlayerHouse ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypePlayerHouse', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locHomesteadLabel')}
          description={t('settings.scenes.locHomesteadDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_BYOH_LocTypeHomestead ?? true}
            onChange={(val) => updateSetting('enableScenes_BYOH_LocTypeHomestead', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>

      {/* Allowed Locations: Streets & Outdoors */}
      <SectionCard title={t('settings.scenes.outdoorsTitle')} icon={MapPin}>
        <SettingRow
          label={t('settings.scenes.locTownLabel')}
          description={t('settings.scenes.locTownDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeTown ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeTown', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locSettlementLabel')}
          description={t('settings.scenes.locSettlementDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeSettlement ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeSettlement', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locStrongholdLabel')}
          description={t('settings.scenes.locStrongholdDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeOrcStronghold ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeOrcStronghold', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locCityLabel')}
          description={t('settings.scenes.locCityDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeCity ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeCity', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locLumberMillLabel')}
          description={t('settings.scenes.locLumberMillDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeLumberMill ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeLumberMill', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.scenes.locFarmLabel')}
          description={t('settings.scenes.locFarmDesc')}
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeFarm ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeFarm', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>
      </SectionCard>
    </div>
  )
}
