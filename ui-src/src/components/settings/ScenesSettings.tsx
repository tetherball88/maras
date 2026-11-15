import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Clapperboard, HeartHandshake, Flame, Info, Building, Home, MapPin } from 'lucide-react'

export const ScenesSettings: React.FC = () => {
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
            NPC Banter & Romance Scenes
          </div>
          <div>
            When entering locations, M.A.R.A.S dynamically selects eligible NPC couples
            to play immersive voiced(if you are using SkyrimNet) interaction scenes that reflect their current relationship status or illicit affairs.
          </div>
        </div>
      </div>

      {/* Master Toggle & General Rules */}
      <SectionCard title="General Scene Rules" icon={Clapperboard}>
        <SettingRow
          label="Enable NPC Scenes"
          description="Master toggle for all relationship and encounter scenes. If disabled, no couples will be coordinated or prompted into scenes."
        >
          <ToggleSwitch
            checked={masterEnabled}
            onChange={(val) => updateSetting('enableScenes', val)}
          />
        </SettingRow>

        <SettingRow
          label="Scene Cooldown"
          description="Minimum game hours that must pass before another NPC scene can trigger (default 24 hours = at most one scene per day)."
          disabled={isMasterDisabled}
        >
          <SliderInput
            value={settings.scenesCooldownHours ?? 24}
            min={0}
            max={168}
            step={1}
            unit=" hrs"
            disabled={isMasterDisabled}
            onChange={(val) => updateSetting('scenesCooldownHours', val)}
          />
        </SettingRow>
      </SectionCard>

      {/* Standard Relationship Scenes */}
      <SectionCard title="Relationship Scenes" icon={HeartHandshake}>
        <SettingRow
          label="Dating Scenes"
          description="Enable conversational scenes between NPCs who are actively dating."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Dating ?? true}
            onChange={(val) => updateSetting('enableScenes_Dating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Lover Scenes"
          description="Enable intimate and affectionate banter scenes between established lovers."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Lover ?? true}
            onChange={(val) => updateSetting('enableScenes_Lover', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Engagement Scenes"
          description="Enable scenes between betrothed couples planning their union."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Engaged ?? true}
            onChange={(val) => updateSetting('enableScenes_Engaged', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Married Scenes"
          description="Enable domestic and affectionate scenes between married spouses."
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
      <SectionCard title="Complex & Affair Scenes" icon={Flame}>
        <SettingRow
          label="Open Relationship Scenes"
          description="Enable scenes between partners in open relationships discussing their dynamic or other partners."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_OpenRelationship ?? true}
            onChange={(val) => updateSetting('enableScenes_OpenRelationship', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Cheating: Secret Affair Scenes"
          description="Enable clandestine dialogue between unfaithful partners meeting in secret."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_Cheating ?? true}
            onChange={(val) => updateSetting('enableScenes_Cheating', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Cheating: Discovered Scenes"
          description="Enable dramatic confrontation scenes when an affair is discovered by a primary partner."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_CheatingDiscovered ?? true}
            onChange={(val) => updateSetting('enableScenes_CheatingDiscovered', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Cheating: Caught in the Act Scenes"
          description="Enable walk-in encounters where cheaters are caught mid-act (requires sex framework and interior bed)."
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
      <SectionCard title="Allowed Locations: Interiors" icon={Building}>
        <SettingRow
          label="Temples (LocTypeTemple)"
          description="Allow scenes inside temples and sacred places of worship."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeTemple ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeTemple', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Stores & Shops (LocTypeStore)"
          description="Allow scenes inside merchant shops, apothecaries, and trade stalls."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeStore ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeStore', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Jarl & Steward Dwellings (LocTypeStewardsDwelling)"
          description="Allow scenes inside jarl keeps, steward chambers, and court palaces."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeStewardsDwelling ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeStewardsDwelling', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Taverns & Inns (LocTypeInn)"
          description="Allow scenes inside taverns, inns, and public drinking houses."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeInn ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeInn', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Private Houses (LocTypeHouse)"
          description="Allow scenes inside residential homes and civilian quarters."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeHouse ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeHouse', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Barracks (LocTypeBarracks)"
          description="Allow scenes inside city guard barracks and military quarters."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeBarracks ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeBarracks', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Guild Halls (LocTypeGuild)"
          description="Allow scenes inside guild halls, sanctuaries, and faction headquarters."
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
      <SectionCard title="Allowed Locations: Player Houses" icon={Home}>
        <SettingRow
          label="Player Homes (LocTypePlayerHouse)"
          description="Allow scenes inside purchased player-owned homes (e.g., Breezehome, Proudspire)."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypePlayerHouse ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypePlayerHouse', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Hearthfire Homesteads (BYOH_LocTypeHomestead)"
          description="Allow scenes inside player-built Hearthfire manors (e.g., Lakeview, Heljarchen, Windstad)."
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
      <SectionCard title="Allowed Locations: Streets & Outdoors" icon={MapPin}>
        <SettingRow
          label="Towns (LocTypeTown)"
          description="Allow outdoor street scenes in towns (e.g., Riverwood, Falkreath, Dawnstar)."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeTown ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeTown', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Settlements & Villages (LocTypeSettlement)"
          description="Allow outdoor street scenes in small settlements, mining camps, and mills."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeSettlement ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeSettlement', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Orc Strongholds (LocTypeOrcStronghold)"
          description="Allow outdoor scenes inside Orc strongholds (e.g., Dushnikh Yal, Narzulbur)."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeOrcStronghold ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeOrcStronghold', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Major Cities (LocTypeCity)"
          description="Allow outdoor street scenes in major walled cities (e.g., Whiterun, Solitude, Windhelm)."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeCity ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeCity', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Lumber Mills (LocTypeLumberMill)"
          description="Allow outdoor scenes around operating lumber mills."
          disabled={isMasterDisabled}
        >
          <ToggleSwitch
            checked={settings.enableScenes_LocTypeLumberMill ?? true}
            onChange={(val) => updateSetting('enableScenes_LocTypeLumberMill', val)}
            disabled={isMasterDisabled}
          />
        </SettingRow>

        <SettingRow
          label="Farms (LocTypeFarm)"
          description="Allow outdoor scenes around rural farms and agrarian homesteads."
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
