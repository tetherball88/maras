import React from 'react'
import { useTranslation } from '../../../i18n'
import { useMarasStore } from '../../../store/useMarasStore'
import { SocialClass, SkillType, Temperament } from '../../../types'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import {
  SocialClassIcon,
  SkillTypeIcon,
  TemperamentIcon,
  getSocialClassLabel,
  getSkillTypeLabel,
  getTemperamentLabel,
} from '../../shared/TraitIcons'
import { Shield } from 'lucide-react'

interface ProfileCharacteristicsSectionProps {
  npcId: string
  className?: string
}

export const ProfileCharacteristicsSection: React.FC<ProfileCharacteristicsSectionProps> = ({
  npcId,
  className = '',
}) => {
  const { t, tp } = useTranslation()
  void tp
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const npc = npcProfiles.get(npcId)

  if (!npc) return null

  return (
    <SectionCard title={t('npcDetail.characteristics.title')} icon={Shield} className={className}>
      <div className="pt-[0.25rem] space-y-[0.5rem]">
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-[0.5rem] text-xs">
          <div>
            <label className="text-foreground-muted block mb-[0.125rem]">
              {t('npcDetail.characteristics.socialClass')}
            </label>
            <SelectInput
              value={String(npc.socialClass)}
              options={Object.values(SocialClass)
                .filter((v): v is SocialClass => typeof v === 'number')
                .map((val) => ({
                  value: String(val),
                  label: getSocialClassLabel(val),
                  icon: (
                    <SocialClassIcon
                      value={val}
                      size="md"
                      showTooltip={false}
                    />
                  ),
                }))}
              onChange={(val) =>
                updateNPCProfile(npcId, { socialClass: parseInt(val) })
              }
              className="w-full text-xs"
            />
          </div>

          <div>
            <label className="text-foreground-muted block mb-[0.125rem]">
              {t('npcDetail.characteristics.skillType')}
            </label>
            <SelectInput
              value={String(npc.skillType)}
              options={Object.values(SkillType)
                .filter((v): v is SkillType => typeof v === 'number')
                .map((val) => ({
                  value: String(val),
                  label: getSkillTypeLabel(val),
                  icon: (
                    <SkillTypeIcon
                      value={val}
                      size="md"
                      showTooltip={false}
                    />
                  ),
                }))}
              onChange={(val) =>
                updateNPCProfile(npcId, { skillType: parseInt(val) })
              }
              className="w-full text-xs"
            />
          </div>

          <div>
            <label className="text-foreground-muted block mb-[0.125rem]">
              {t('npcDetail.characteristics.temperament')}
            </label>
            <SelectInput
              value={String(npc.temperament)}
              options={Object.values(Temperament)
                .filter((v): v is Temperament => typeof v === 'number')
                .map((val) => ({
                  value: String(val),
                  label: getTemperamentLabel(val),
                  icon: (
                    <TemperamentIcon
                      value={val}
                      size="md"
                      showTooltip={false}
                    />
                  ),
                }))}
              onChange={(val) =>
                updateNPCProfile(npcId, { temperament: parseInt(val) })
              }
              className="w-full text-xs"
            />
          </div>
        </div>
      </div>
    </SectionCard>
  )
}
