import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SliderInput } from '../shared/SliderInput'
import { SectionCard } from '../shared/SectionCard'
import { Baby, GraduationCap } from 'lucide-react'

export const ChildrenSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const trainingDays = settings.childTrainingDurationDays ?? 14
  const trainingCost = settings.childTrainingCost ?? 2000

  return (
    <div className="space-y-[1rem]">
      <SectionCard title={t('settings.children.trainingTitle')} icon={GraduationCap}>
        <SettingRow
          label={t('settings.children.trainingDurationLabel')}
          description={tp('settings.children.trainingDurationDesc', trainingDays)}
        >
          <SliderInput
            value={trainingDays}
            min={1}
            max={120}
            step={1}
            unit={t('settings.children.unitDaysShort')}
            onChange={(val) => updateSetting('childTrainingDurationDays', Math.round(val))}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.children.trainingCostLabel')}
          description={t('settings.children.trainingCostDesc', { cost: trainingCost })}
        >
          <SliderInput
            value={trainingCost}
            min={0}
            max={10000}
            step={100}
            unit={t('settings.children.unitGoldShort')}
            onChange={(val) => updateSetting('childTrainingCost', Math.round(val))}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.children.requireFactionLabel')}
          description={t('settings.children.requireFactionDesc')}
        >
          <ToggleSwitch
            checked={settings.childTrainingRequireFaction ?? true}
            onChange={(val) => updateSetting('childTrainingRequireFaction', val)}
          />
        </SettingRow>
      </SectionCard>

      <SectionCard title={t('settings.children.petAndCustodyTitle')} icon={Baby}>
        <SettingRow
          label={t('settings.children.globalBanCritterLabel')}
          description={t('settings.children.globalBanCritterDesc')}
        >
          <ToggleSwitch
            checked={!!settings.globalBanCritter}
            onChange={(val) => updateSetting('globalBanCritter' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.children.globalBanPetLabel')}
          description={t('settings.children.globalBanPetDesc')}
        >
          <ToggleSwitch
            checked={!!settings.globalBanPet}
            onChange={(val) => updateSetting('globalBanPet' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.children.prioritizeBioParentLabel')}
          description={t('settings.children.prioritizeBioParentDesc')}
        >
          <ToggleSwitch
            checked={settings.childCustodyPrioritizeBioParent !== false}
            onChange={(val) => updateSetting('childCustodyPrioritizeBioParent' as any, val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.children.genderCustodyLabel')}
          description={t('settings.children.genderCustodyDesc')}
        >
          <select
            value={settings.childCustodyGenderPolicy ?? 0}
            onChange={(e) =>
              updateSetting('childCustodyGenderPolicy' as any, parseInt(e.target.value, 10))
            }
            className="min-w-[13rem] w-auto bg-panel-inset text-foreground-primary text-xs border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus:outline-none focus:border-border-accent"
          >
            <option value={0}>{t('settings.children.custodyStayMother')}</option>
            <option value={1}>{t('settings.children.custodyStayFather')}</option>
          </select>
        </SettingRow>

        <SettingRow
          label={t('settings.children.playerCustodyLabel')}
          description={t('settings.children.playerCustodyDesc')}
        >
          <select
            value={settings.childCustodyPlayerPolicy ?? 0}
            onChange={(e) =>
              updateSetting('childCustodyPlayerPolicy' as any, parseInt(e.target.value, 10))
            }
            className="min-w-[13rem] w-auto bg-panel-inset text-foreground-primary text-xs border border-border-subtle rounded px-[0.5rem] py-[0.25rem] focus:outline-none focus:border-border-accent"
          >
            <option value={0}>{t('settings.children.custodyUseGender')}</option>
            <option value={1}>{t('settings.children.custodyAlwaysPlayer')}</option>
            <option value={2}>{t('settings.children.custodyNeverPlayer')}</option>
          </select>
        </SettingRow>
      </SectionCard>
    </div>
  )
}
