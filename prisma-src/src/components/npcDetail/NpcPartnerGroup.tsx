import React from 'react'
import { RelationshipPair, RelationshipStatus, AffairStatus, AffairStage } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge } from '../shared/StatusBadge'
import { AffectionBar } from '../shared/AffectionBar'
import { AppTooltip } from '../shared/AppTooltip'
import { useMarasStore } from '../../store/useMarasStore'
import { confirmRemoveRelationship } from '../../store/useModalStore'
import { Heart, User, Crown, AlertTriangle, Shield, Clock, Flame, Trash2 } from 'lucide-react'

interface NpcPartnerGroupProps {
  title: string
  icon?: React.ElementType
  badgeColor?: string
  relationships: RelationshipPair[]
  currentNpcId: string
  onSelectPartner: (partnerId: string) => void
}

export const NpcPartnerGroup: React.FC<NpcPartnerGroupProps> = ({
  title,
  icon: Icon = Heart,
  badgeColor = 'text-accent',
  relationships,
  currentNpcId,
  onSelectPartner,
}) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const affectionData = useMarasStore((s) => s.affectionData)
  const cheatingRecords = useMarasStore((s) => s.cheatingRecords)
  const removeRelationship = useMarasStore((s) => s.removeRelationship)

  const currentNpcName =
    currentNpcId === PLAYER_FORM_ID
      ? npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
      : npcProfiles.get(currentNpcId)?.name || `NPC ${currentNpcId}`

  const validRelationships = relationships.filter((r) => {
    const partnerId = r.actorA === currentNpcId ? r.actorB : r.actorA
    return Boolean(partnerId && partnerId !== '' && partnerId !== '00000000' && partnerId !== currentNpcId)
  })

  if (validRelationships.length === 0) return null

  return (
    <div className="space-y-[0.375rem]">
      <div className="text-xs font-semibold text-foreground-secondary flex items-center gap-[0.375rem] border-b border-border-divider pb-[0.25rem]">
        <Icon className={`w-[1em] h-[1em] ${badgeColor}`} />
        <span>{title}</span>
        <span className="text-foreground-muted font-normal">({validRelationships.length})</span>
      </div>

      <div className="grid grid-cols-1 gap-[0.375rem]">
        {validRelationships.map((r, idx) => {
          const partnerId = r.actorA === currentNpcId ? r.actorB : r.actorA
          const isPlayer = partnerId === PLAYER_FORM_ID
          const partnerProfile = isPlayer ? npcProfiles.get(PLAYER_FORM_ID) : npcProfiles.get(partnerId)
          const partnerName = isPlayer ? (npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)') : partnerProfile?.name || `NPC ${partnerId}`
          const partnerRace = isPlayer ? (npcProfiles.get(PLAYER_FORM_ID)?.race || 'Nord') : partnerProfile?.race || 'Nord'
          const partnerPlugin = isPlayer ? (npcProfiles.get(PLAYER_FORM_ID)?.pluginName || 'Skyrim.esm') : partnerProfile?.pluginName
          const isPlayerInvolved = currentNpcId === PLAYER_FORM_ID || partnerId === PLAYER_FORM_ID
          const targetNpcId = currentNpcId === PLAYER_FORM_ID ? partnerId : currentNpcId
          const aff = affectionData.get(targetNpcId)?.permanent ?? 0
          const hasAffection = isPlayerInvolved && (
            r.status === RelationshipStatus.Married ||
            r.status === RelationshipStatus.Engaged ||
            r.status === RelationshipStatus.Dating ||
            r.status === RelationshipStatus.Lover
          )

          const partnerCheatedOnMeRecords = cheatingRecords.filter((cr) => cr.cheaterId === partnerId && cr.victimId === currentNpcId)
          const affairRecords = cheatingRecords.filter(
            (cr) =>
              cr.cheaterId === currentNpcId &&
              cr.affairPartnerId === partnerId &&
              (cr.affairStatus === undefined || cr.affairStatus === AffairStatus.Ongoing)
          )
          const affairIncidents = affairRecords.reduce((sum, cr) => sum + (cr.count || 1), 0)
          const isTalkingAffair = affairRecords.some((cr) => cr.affairStage === AffairStage.Talking)
          const isUnofficialAffair = affairRecords.length > 0 && (r.status === RelationshipStatus.Candidate || r.status === RelationshipStatus.Unknown)

          return (
            <div
              key={idx}
              onClick={() => onSelectPartner(partnerId)}
              className="bg-panel-raised border border-border-subtle hover:border-border-strong hover:bg-interactive-hover rounded-md p-[0.5rem] flex items-center justify-between gap-[0.75rem] transition-all cursor-pointer"
            >
              <div className="flex items-center gap-[0.625rem] min-w-0 flex-1">
                {isPlayer ? (
                  <Crown className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                ) : (
                  <User className="w-[1em] h-[1em] text-foreground-muted flex-shrink-0" />
                )}
                <div className="min-w-0">
                  <div className="text-xs font-semibold text-foreground-primary truncate">
                    {partnerName}
                  </div>
                  <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.375rem] flex-wrap">
                    <span className="font-mono">{partnerId}</span>
                    <span>•</span>
                    <span>{partnerRace}</span>
                    {partnerPlugin && (
                      <>
                        <span>•</span>
                        <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[6.5rem]" title={`Plugin: ${partnerPlugin}`}>
                          {partnerPlugin}
                        </span>
                      </>
                    )}
                  </div>
                </div>
              </div>

              {/* Show affection bar if in romantic relationship with player (Married, Engaged, Dating, Lover) */}
              {hasAffection && (
                <div className="w-[6rem] flex-shrink-0 hidden sm:block">
                  <AffectionBar value={aff} size="sm" />
                </div>
              )}

              <div className="flex-shrink-0 flex items-center gap-[0.375rem]">
                {partnerCheatedOnMeRecords.length > 0 && (
                  <span className="text-[0.625rem] px-[0.375rem] py-[0.125rem] rounded bg-rose-500/20 text-rose-400 border border-rose-500/30 font-semibold flex items-center gap-[0.25rem]">
                    <AlertTriangle className="w-[0.875em] h-[0.875em]" />
                    <span>Cheated ({partnerCheatedOnMeRecords.length})</span>
                  </span>
                )}
                {affairRecords.length > 0 && !isUnofficialAffair && (
                  <span className="text-[0.625rem] px-[0.375rem] py-[0.125rem] rounded bg-purple-500/20 text-purple-300 border border-purple-500/30 font-semibold flex items-center gap-[0.25rem]">
                    <Flame className="w-[0.875em] h-[0.875em]" />
                    <span>{isTalkingAffair ? 'Affair (Talking)' : `Affair (${affairIncidents})`}</span>
                  </span>
                )}
                {isUnofficialAffair && (
                  <span className="text-[0.625rem] px-[0.375rem] py-[0.125rem] rounded bg-rose-500/20 text-rose-300 border border-rose-500/30 font-semibold flex items-center gap-[0.25rem]">
                    <Flame className="w-[0.875em] h-[0.875em]" />
                    <span>Affair Partner{affairIncidents > 1 ? ` (${affairIncidents})` : ''}</span>
                  </span>
                )}
                {r.isOpen && (
                  <span className="text-[0.625rem] px-[0.375rem] py-[0.125rem] rounded bg-emerald-500/20 text-emerald-400 border border-emerald-500/30 font-semibold">
                    Open
                  </span>
                )}
                {!isUnofficialAffair && <StatusBadge status={r.status} />}
                <AppTooltip content="Remove Relationship Record">
                  <button
                    onClick={(e) => {
                      e.stopPropagation()
                      confirmRemoveRelationship(currentNpcName, partnerName, () =>
                        removeRelationship(currentNpcId, partnerId),
                      )
                    }}
                    className="p-[0.25rem] rounded text-foreground-muted hover:text-status-danger hover:bg-rose-950/40 transition-colors cursor-pointer"
                  >
                    <Trash2 className="w-[0.95em] h-[0.95em]" />
                  </button>
                </AppTooltip>
              </div>
            </div>
          )
        })}
      </div>
    </div>
  )
}

