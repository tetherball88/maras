import React from 'react'
import { useTranslation } from '../../../i18n'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import { Award } from 'lucide-react'

interface SpouseHierarchySectionProps {
  npcId: string
  className?: string
}

export const SpouseHierarchySection: React.FC<SpouseHierarchySectionProps> = ({
  npcId,
  className = '',
}) => {
  const { t, tp } = useTranslation()
  void tp
  const hierarchyRanks = useMarasStore((s) => s.hierarchyRanks)
  const setHierarchyRank = useMarasStore((s) => s.setHierarchyRank)

  const currentRank = hierarchyRanks.indexOf(npcId)

  const rankOptions = [
    { value: 'none', label: t('npcDetail.hierarchy.optionUnranked') },
    { value: '0', label: t('npcDetail.hierarchy.optionRank1') },
    { value: '1', label: t('npcDetail.hierarchy.optionRank2') },
    { value: '2', label: t('npcDetail.hierarchy.optionRank3') },
  ]

  const rankSubtitle =
    currentRank === 0
      ? t('npcDetail.hierarchy.rank1')
      : currentRank === 1
        ? t('npcDetail.hierarchy.rank2')
        : currentRank === 2
          ? t('npcDetail.hierarchy.rank3')
          : currentRank > 2
            ? t('npcDetail.hierarchy.rankN', { rank: currentRank + 1 })
            : t('npcDetail.hierarchy.unranked')

  return (
    <SectionCard
      title={t('npcDetail.hierarchy.title')}
      icon={Award}
      subtitle={rankSubtitle}
      className={className}
    >
      <div className="space-y-[0.5rem] pt-[0.25rem] flex flex-col justify-between h-full">
        <SelectInput
          value={currentRank === -1 ? 'none' : currentRank.toString()}
          options={rankOptions}
          onChange={(val) =>
            setHierarchyRank(npcId, val === 'none' ? -1 : parseInt(val))
          }
          className="w-full"
        />
        <div className="text-[0.6875rem] text-foreground-muted pt-[0.25rem]">
          {t('npcDetail.hierarchy.description')}
        </div>
      </div>
    </SectionCard>
  )
}
