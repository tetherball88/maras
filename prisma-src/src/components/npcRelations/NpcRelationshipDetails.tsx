import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge, getStatusLabel, StatusIcon } from '../shared/StatusBadge'
import { formatRelationshipDate, getLatestStatusRecord } from '../../utils/dateUtils'
import { AffectionBar } from '../shared/AffectionBar'
import { ScrollArea } from '@/components/ui/scroll-area'
import { GitFork, Heart, Calendar, Shield, Baby } from 'lucide-react'
import { resolveIsFemale, getChildGenderNouns } from '../../utils/childUtils'
import { areSameFormId } from '../../utils/statusAvailability'

export const NpcRelationshipDetails: React.FC = () => {
  const selectedId = useMarasStore((s) => s.selectedNpcRelationsId)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const relationships = useMarasStore((s) => s.relationships)
  const children = useMarasStore((s) => s.children)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const affectionData = useMarasStore((s) => s.affectionData)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)

  if (!selectedId) {
    return (
      <div className="flex-1 bg-panel border border-border-subtle rounded-lg flex items-center justify-center p-[2rem] text-center text-foreground-muted">
        <div className="flex flex-col items-center gap-[0.5rem]">
          <GitFork className="w-[2em] h-[2em] text-accent" />
          <span>Select an NPC from the list to view their relationship breakdown</span>
        </div>
      </div>
    )
  }

  const playerDisplayName = npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'

  const selectedNpc =
    selectedId === PLAYER_FORM_ID
      ? (npcProfiles.get(PLAYER_FORM_ID) || { formId: PLAYER_FORM_ID, name: playerDisplayName, race: npcProfiles.get(PLAYER_FORM_ID)?.race || 'Nord', gender: npcProfiles.get(PLAYER_FORM_ID)?.gender || 'female', isChild: false })
      : npcProfiles.get(selectedId)

  const childRel = children.get(selectedId)
  const isChild = !!(selectedNpc?.isChild || childRel)

  const isFemale = resolveIsFemale(childRel, selectedNpc)
  const { sonOrDaughter } = getChildGenderNouns(isFemale)

  // Find all relationships involving selectedId
  const rels = relationships.filter(
    (r) => r.actorA === selectedId || r.actorB === selectedId,
  )

  const getName = (id: string) => {
    if (id === PLAYER_FORM_ID) return playerDisplayName
    return npcProfiles.get(id)?.name || 'Unknown NPC'
  }

  const getRace = (id: string) => {
    if (id === PLAYER_FORM_ID) return npcProfiles.get(PLAYER_FORM_ID)?.race || 'Nord'
    return npcProfiles.get(id)?.race || 'Unknown'
  }

  // Status order for grouping
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
    <div className="flex-1 bg-panel border border-border-subtle rounded-lg flex flex-col h-full overflow-hidden">
      {/* Header */}
      <header className="p-[1rem] bg-panel-raised border-b border-border-divider flex items-center justify-between">
        <div>
          <h2 className="text-lg font-bold text-accent-bright flex items-center gap-[0.375rem]">
            <GitFork className="w-[0.875em] h-[0.875em] text-accent" /> <span>{selectedNpc?.name}</span>
            {selectedNpc?.formId && (
              <span className="text-xs font-mono font-normal text-foreground-muted bg-panel-inset px-[0.375rem] py-[0.125rem] rounded border border-border-subtle">
                {selectedNpc.formId}
              </span>
            )}
          </h2>
          <div className="text-xs text-foreground-secondary mt-[0.125rem]">
            {selectedNpc?.race} • {isChild ? `Household ${sonOrDaughter} Dependent` : `${rels.length} Tracked Relationship Pair${rels.length === 1 ? '' : 's'}`}
          </div>
        </div>

        {selectedId && (
          <div className="flex items-center gap-[0.5rem]">
            <button
              onClick={() => openNpcDetail(selectedId)}
              className="text-xs text-accent-bright bg-accent-soft hover:bg-accent/20 px-[0.5rem] py-[0.25rem] rounded border border-border-accent transition-colors flex items-center gap-[0.25rem] cursor-pointer font-semibold"
              title={`View details for ${selectedNpc?.name}`}
            >
              <span>View Full Page</span>
            </button>
          </div>
        )}
      </header>

      {/* Relationship Grouped List */}
      <ScrollArea className="flex-1 min-h-0 p-[1rem]">
        <div className="space-y-[1rem] pr-[0.5rem]">
          {isChild ? (
            <div className="flex flex-col items-center justify-center py-[4rem] text-center space-y-[0.75rem]">
              <div className="p-[0.75rem] rounded-full bg-amber-500/20 text-amber-300 border border-amber-500/30">
                <Baby className="w-[1.75em] h-[1.75em]" />
              </div>
              <div className="text-sm font-semibold text-foreground-primary">
                Household {sonOrDaughter} Dependent
              </div>
              <div className="text-xs text-foreground-muted max-w-md">
                {selectedNpc?.name || `NPC ${selectedId}`} is registered as a {sonOrDaughter.toLowerCase()} dependent. Children cannot participate in tracked romantic relationships.
              </div>
              <button
                onClick={() => openNpcDetail(selectedId)}
                className="mt-[0.5rem] px-[0.75rem] py-[0.375rem] text-xs font-semibold bg-accent-soft text-accent-bright border border-border-accent rounded hover:bg-accent/20 transition-colors cursor-pointer"
              >
                View {sonOrDaughter} Profile & Guardians →
              </button>
            </div>
          ) : rels.length === 0 ? (
            <div className="text-center py-[4rem] text-foreground-muted text-sm italic">
              No active or historical relationships recorded for {selectedNpc?.name}.
            </div>
          ) : (
            statusOrder.map((status) => {
              const groupRels = rels.filter((r) => r.status === status)
              if (groupRels.length === 0) return null

              return (
                <div key={status} className="space-y-[0.5rem]">
                  <div className="text-xs font-semibold text-accent-bright border-b border-border-divider pb-[0.25rem] flex items-center gap-[0.375rem]">
                    <StatusIcon status={status} size="1em" />
                    <span>{getStatusLabel(status)} ({groupRels.length})</span>
                  </div>

                  <div className="grid grid-cols-1 gap-[0.5rem]">
                    {groupRels.map((r, idx) => {
                      const otherId = r.actorA === selectedId ? r.actorB : r.actorA
                      const otherName = getName(otherId)
                      const otherRace = getRace(otherId)
                      const isPlayerInvolved = selectedId === PLAYER_FORM_ID || otherId === PLAYER_FORM_ID
                      const npcTargetId = selectedId === PLAYER_FORM_ID ? otherId : selectedId
                      const affection = affectionData.get(npcTargetId)?.permanent ?? 0
                      const hasAffectionStatus = isPlayerInvolved && (
                        r.status === RelationshipStatus.Married ||
                        r.status === RelationshipStatus.Engaged ||
                        r.status === RelationshipStatus.Dating ||
                        r.status === RelationshipStatus.Lover
                      )

                      return (
                        <div
                          key={idx}
                          onClick={() => openNpcDetail(otherId)}
                          className="bg-panel-raised border border-border-subtle rounded-md p-[0.75rem] flex items-center justify-between gap-[1rem] transition-all cursor-pointer hover:border-border-strong hover:bg-interactive-hover"
                          title={`Click to view full page for ${otherName}`}
                        >
                          <div className="flex items-center gap-[0.75rem] min-w-0 flex-1">
                            <div className="p-[0.5rem] rounded-full bg-accent-soft text-accent flex-shrink-0">
                              <Heart className="w-[1.125em] h-[1.125em]" />
                            </div>
                            <div className="min-w-0">
                              <div className="text-sm font-semibold text-foreground-primary truncate">
                                {otherName}
                              </div>
                              <div className="text-xs text-foreground-muted flex items-center gap-[0.5rem]">
                                <span>{otherRace}</span>
                                {(() => {
                                  const lastRec = getLatestStatusRecord(r)
                                  if (!lastRec) return null
                                  const label = getStatusLabel(lastRec.toStatus)
                                  return (
                                    <>
                                      <span>•</span>
                                      <span className="flex items-center gap-[0.25rem]">
                                        <Calendar className="w-[0.875em] h-[0.875em]" />
                                        {formatRelationshipDate(lastRec.timestamp, label, currentGameDay)}
                                      </span>
                                    </>
                                  )
                                })()}
                              </div>
                            </div>
                          </div>

                          {hasAffectionStatus && (
                            <div className="w-[8rem] flex-shrink-0">
                              <AffectionBar value={affection} size="sm" />
                            </div>
                          )}

                          <div className="flex-shrink-0">
                            <StatusBadge status={r.status} />
                          </div>
                        </div>
                      )
                    })}
                  </div>
                </div>
              )
            })
          )}
        </div>
      </ScrollArea>
    </div>
  )
}
