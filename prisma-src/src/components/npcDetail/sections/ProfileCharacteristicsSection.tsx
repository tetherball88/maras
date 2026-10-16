import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SocialClass, SkillType, Temperament } from '../../../types'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import {
  SocialClassIcon,
  SkillTypeIcon,
  TemperamentIcon,
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
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const npc = npcProfiles.get(npcId)

  if (!npc) return null

  return (
    <SectionCard title="Profile Characteristics" icon={Shield} className={className}>
      <div className="pt-[0.25rem] space-y-[0.5rem]">
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-[0.5rem] text-xs">
          <div>
            <label className="text-foreground-muted block mb-[0.125rem]">
              Social Class
            </label>
            <SelectInput
              value={npc.socialClass}
              options={Object.keys(SocialClass)
                .filter((k) => isNaN(Number(k)))
                .map((key, idx) => ({
                  value: idx,
                  label: key,
                  icon: (
                    <SocialClassIcon
                      value={idx}
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
              Skill Type
            </label>
            <SelectInput
              value={npc.skillType}
              options={Object.keys(SkillType)
                .filter((k) => isNaN(Number(k)))
                .map((key, idx) => ({
                  value: idx,
                  label: key,
                  icon: (
                    <SkillTypeIcon
                      value={idx}
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
              Temperament
            </label>
            <SelectInput
              value={npc.temperament}
              options={Object.keys(Temperament)
                .filter((k) => isNaN(Number(k)))
                .map((key, idx) => ({
                  value: idx,
                  label: key,
                  icon: (
                    <TemperamentIcon
                      value={idx}
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
