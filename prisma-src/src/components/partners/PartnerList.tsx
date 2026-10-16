import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { PartnerListItem } from './PartnerListItem'
import { getStatusLabel, StatusIcon } from '../shared/StatusBadge'
import { ScrollArea } from '@/components/ui/scroll-area'
import { ChevronDown, ChevronRight } from 'lucide-react'

export const PartnerList: React.FC = () => {
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const affectionData = useMarasStore((s) => s.affectionData)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const npcDetailId = useMarasStore((s) => s.npcDetailId)

  // Track collapsed status groups
  const [collapsedGroups, setCollapsedGroups] = useState<Record<number, boolean>>({
    [RelationshipStatus.Candidate]: true,
    [RelationshipStatus.Divorced]: true,
    [RelationshipStatus.Jilted]: true,
    [RelationshipStatus.ExDating]: true,
    [RelationshipStatus.ExLover]: true,
  })

  const toggleGroup = (status: RelationshipStatus) => {
    setCollapsedGroups((prev) => ({ ...prev, [status]: !prev[status] }))
  }

  // Get all player relationships
  const playerRelationships = relationships.filter(
    (r) => r.actorA === PLAYER_FORM_ID || r.actorB === PLAYER_FORM_ID,
  )

  // Order of status groups to render
  const statusOrder = [
    RelationshipStatus.Married,
    RelationshipStatus.Lover,
    RelationshipStatus.Dating,
    RelationshipStatus.Engaged,
    RelationshipStatus.Candidate,
    RelationshipStatus.Divorced,
    RelationshipStatus.Jilted,
    RelationshipStatus.ExDating,
    RelationshipStatus.ExLover,
  ]

  return (
    <ScrollArea className="flex-1 min-h-0 pr-[0.5rem] h-full">
      <div className="space-y-[0.75rem]">
        {statusOrder.map((status) => {
          const groupItems = playerRelationships.filter((r) => r.status === status)
          if (groupItems.length === 0) return null

          const isCollapsed = collapsedGroups[status] ?? false
          const label = getStatusLabel(status)

          return (
            <div key={status} className="space-y-[0.375rem]">
              {/* Group Header */}
              <div
                onClick={() => toggleGroup(status)}
                className="flex items-center justify-between p-[0.375rem] px-[0.625rem] bg-panel-inset rounded border border-border-divider cursor-pointer hover:border-border-subtle transition-colors"
              >
                <div className="flex items-center gap-[0.5rem] text-xs font-semibold text-accent-bright">
                  {isCollapsed ? (
                    <ChevronRight className="w-[1em] h-[1em] text-foreground-muted" />
                  ) : (
                    <ChevronDown className="w-[1em] h-[1em] text-accent" />
                  )}
                  <div className="flex items-center gap-[0.375rem]">
                    <StatusIcon status={status} />
                    <span>{label}</span>
                  </div>
                  <span className="text-foreground-muted font-normal">({groupItems.length})</span>
                </div>
              </div>

              {/* Group Content */}
              {!isCollapsed && (
                <div className="space-y-[0.375rem] pl-[0.25rem]">
                  {groupItems.map((r) => {
                    const partnerId = r.actorA === PLAYER_FORM_ID ? r.actorB : r.actorA
                    const npc = npcProfiles.get(partnerId)
                    if (!npc) return null

                    const aff = affectionData.get(partnerId)?.permanent ?? 0
                    const isSelected = npcDetailId === partnerId

                    return (
                      <PartnerListItem
                        key={partnerId}
                        npc={npc}
                        status={r.status}
                        affection={aff}
                        isSelected={isSelected}
                        onClick={() => openNpcDetail(partnerId)}
                      />
                    )
                  })}
                </div>
              )}
            </div>
          )
        })}
      </div>
    </ScrollArea>
  )
}
