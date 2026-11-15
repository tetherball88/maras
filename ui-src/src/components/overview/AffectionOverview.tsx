import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { RelationshipStatus } from '../../types'
import { AffectionBar } from '../shared/AffectionBar'
import { StatusBadge } from '../shared/StatusBadge'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Heart } from 'lucide-react'

export const AffectionOverview: React.FC = () => {
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const affectionData = useMarasStore((s) => s.affectionData)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)

  // Get all romantic partners with affection connected to the player
  const playerRelationships = relationships.filter(
    (r) =>
      (r.actorA === PLAYER_FORM_ID || r.actorB === PLAYER_FORM_ID) &&
      (r.status === RelationshipStatus.Married ||
        r.status === RelationshipStatus.Engaged ||
        r.status === RelationshipStatus.Dating ||
        r.status === RelationshipStatus.Lover),
  )

  const partners = playerRelationships
    .map((r) => {
      const partnerId = r.actorA === PLAYER_FORM_ID ? r.actorB : r.actorA
      const npc = npcProfiles.get(partnerId)
      const aff = affectionData.get(partnerId)?.permanent ?? 0
      return { partnerId, npc, status: r.status, affection: aff }
    })
    .filter((p) => p.npc !== undefined)
    .sort((a, b) => b.affection - a.affection)

  const handleRowClick = (id: string) => {
    openNpcDetail(id)
  }

  return (
    <div className="bg-panel border border-border-subtle rounded-lg p-[1rem] flex flex-col gap-[0.75rem] h-full">
      <div className="flex items-center justify-between border-b border-border-divider pb-[0.5rem]">
        <h3 className="text-sm font-semibold text-accent-bright flex items-center gap-[0.375rem]">
          <Heart className="w-[1.125em] h-[1.125em] text-accent" /> Affection Health
        </h3>
        <span className="text-xs text-foreground-muted">{partners.length} Partners Tracked</span>
      </div>

      <ScrollArea className="h-[16rem] pr-[0.5rem]">
        <div className="space-y-[0.5rem]">
          {partners.length === 0 ? (
            <div className="text-xs text-foreground-muted text-center py-[2rem]">No active partners</div>
          ) : (
            partners.map(({ partnerId, npc, status, affection }) => (
              <div
                key={partnerId}
                onClick={() => handleRowClick(partnerId)}
                className="flex items-center gap-[0.75rem] p-[0.5rem] bg-panel-raised border border-border-subtle hover:border-border-accent rounded-md transition-colors cursor-pointer"
              >
                <div className="w-[9rem] flex-shrink-0">
                  <div className="text-xs font-semibold text-foreground-primary truncate">{npc?.name}</div>
                  <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem] truncate">
                    <span>{npc?.race}</span>
                    {npc?.pluginName && (
                      <>
                        <span>•</span>
                        <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[4.5rem]" title={`Plugin: ${npc.pluginName}`}>
                          {npc.pluginName}
                        </span>
                      </>
                    )}
                  </div>
                </div>

                <div className="flex-1">
                  <AffectionBar value={affection} size="sm" />
                </div>

                <div className="flex-shrink-0">
                  <StatusBadge status={status} />
                </div>
              </div>
            ))
          )}
        </div>
      </ScrollArea>
    </div>
  )
}
