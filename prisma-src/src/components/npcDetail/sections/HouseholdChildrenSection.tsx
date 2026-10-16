import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { Baby } from 'lucide-react'
import { formatGameDay } from '../../../utils/dateUtils'
import { getChildRelationshipStatus } from '../../../utils/childUtils'
import { areSameFormId } from '../../../utils/statusAvailability'

interface HouseholdChildrenSectionProps {
  npcId: string
  className?: string
}

export const HouseholdChildrenSection: React.FC<HouseholdChildrenSectionProps> = ({
  npcId,
  className = '',
}) => {
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const children = useMarasStore((s) => s.children)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)

  const allChildren = Array.from(children.values()).filter(
    (c) =>
      areSameFormId(c.guardianAId, npcId) ||
      areSameFormId(c.guardianBId, npcId) ||
      areSameFormId(c.bioMotherId, npcId) ||
      areSameFormId(c.bioFatherId, npcId),
  )

  if (allChildren.length === 0) return null

  return (
    <SectionCard
      title={`Household Children & Offspring (${allChildren.length})`}
      icon={Baby}
      className={className}
    >
      <div className="pt-[0.25rem] space-y-[0.5rem] text-xs">
        <div className="space-y-[0.375rem] max-h-[22rem] overflow-y-auto pr-[0.25rem]">
          {allChildren.map((child, idx) => {
            const childProfile = npcProfiles.get(child.childId)
            const isPlaceholderName = !childProfile?.name || childProfile.name.startsWith('NPC_')
            const childName = (isPlaceholderName && child.name && !child.name.startsWith('NPC_'))
              ? child.name
              : (childProfile?.name || child.name || `Child ${child.childId}`)
            const { statusLabel, badgeLabel, badgeStyle } = getChildRelationshipStatus(
              child,
              npcId,
            )

            return (
              <div
                key={idx}
                onClick={() => openNpcDetail(child.childId)}
                className="p-[0.5rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded flex items-center justify-between transition-colors cursor-pointer"
              >
                <div className="flex items-center gap-[0.5rem] min-w-0">
                  <Baby className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                  <div className="min-w-0">
                    <div className="font-semibold text-foreground-primary text-xs flex items-center gap-[0.375rem] truncate">
                      <span className="truncate">{childName}</span>
                      <span className="text-[0.625rem] font-mono text-foreground-muted flex-shrink-0">
                        {child.childId}
                      </span>
                    </div>
                    <span className="text-[0.6875rem] text-foreground-muted truncate block">
                      {statusLabel}
                      {child.timestamp != null &&
                        ` • ${formatGameDay(child.timestamp, currentGameDay)}`}
                    </span>
                  </div>
                </div>

                <span
                  className={`px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold flex-shrink-0 ml-[0.5rem] ${badgeStyle}`}
                >
                  {badgeLabel}
                </span>
              </div>
            )
          })}
        </div>
      </div>
    </SectionCard>
  )
}
