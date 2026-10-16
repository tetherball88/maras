import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import { Award } from 'lucide-react'

interface SpouseHierarchySectionProps {
  npcId: string
  className?: string
}

const rankOptions = [
  { value: 'none', label: 'Unranked (4th+)' },
  { value: '0', label: '1st Rank Spouse' },
  { value: '1', label: '2nd Rank Spouse' },
  { value: '2', label: '3rd Rank Spouse' },
]

export const SpouseHierarchySection: React.FC<SpouseHierarchySectionProps> = ({
  npcId,
  className = '',
}) => {
  const hierarchyRanks = useMarasStore((s) => s.hierarchyRanks)
  const setHierarchyRank = useMarasStore((s) => s.setHierarchyRank)

  const currentRank = hierarchyRanks.indexOf(npcId)

  return (
    <SectionCard
      title="Spouse Hierarchy Rank"
      icon={Award}
      subtitle={currentRank >= 0 ? `${currentRank + 1}st Rank` : 'Unranked'}
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
          Determines household priority & living arrangement order.
        </div>
      </div>
    </SectionCard>
  )
}
