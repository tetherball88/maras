import React, { useMemo } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import {
  RelationshipPair,
  RelationshipStatus,
  SocialClass,
  SkillType,
  Temperament,
  AffairType,
  AffairStatus,
} from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge, getStatusLabel } from '../shared/StatusBadge'
import { SliderInput } from '../shared/SliderInput'
import { SelectInput } from '../shared/SelectInput'
import { SectionCard } from '../shared/SectionCard'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { ScrollArea } from '@/components/ui/scroll-area'
import { categorizePartners } from '../../utils/partnerCategories'
import { X, Heart, Home, Shield, Award, Users, ExternalLink, Crown, Baby, Trash2, Flame } from 'lucide-react'
import { AppTooltip } from '../shared/AppTooltip'
import { formatGameDay } from '../../utils/dateUtils'
import { getChildRelationshipStatus } from '../../utils/childUtils'
import { useAvailableStatuses, areSameFormId } from '../../utils/statusAvailability'
import { getPlayerProfileFallback } from '../../utils/playerUtils'

export const NpcDetailPartnerPanel: React.FC = () => {
  const currentNpcId = useMarasStore((s) => s.npcDetailId)
  const partnerId = useMarasStore((s) => s.npcDetailPartnerId)
  const setPartnerId = useMarasStore((s) => s.setNpcDetailPartnerId)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)

  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const affectionData = useMarasStore((s) => s.affectionData)
  const hierarchyRanks = useMarasStore((s) => s.hierarchyRanks)
  const playerHomes = useMarasStore((s) => s.playerHomes)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)
  const cheatingRecords = useMarasStore((s) => s.cheatingRecords)
  const children = useMarasStore((s) => s.children)
  const settings = useMarasStore((s) => s.settings)

  const updateAffection = useMarasStore((s) => s.updateAffection)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const setHierarchyRank = useMarasStore((s) => s.setHierarchyRank)
  const assignRelationshipStatus = useMarasStore((s) => s.assignRelationshipStatus)
  const removeRelationship = useMarasStore((s) => s.removeRelationship)
  const removeChild = useMarasStore((s) => s.removeChild)

  const isOpen = Boolean(partnerId)
  const isPlayerPartner = partnerId === PLAYER_FORM_ID
  const availableStatusOptions = useAvailableStatuses(currentNpcId, partnerId)

  const partnerProfile = partnerId
    ? isPlayerPartner
      ? getPlayerProfileFallback(npcProfiles)
      : npcProfiles.get(partnerId)
    : null

  // Relationship between current NPC and this partner
  const foundRel = partnerId && currentNpcId
    ? relationships.find(
      (r) =>
        (areSameFormId(r.actorA, currentNpcId) && areSameFormId(r.actorB, partnerId)) ||
        (areSameFormId(r.actorA, partnerId) && areSameFormId(r.actorB, currentNpcId)),
    )
    : undefined

  const currentRel: RelationshipPair | undefined = partnerId && currentNpcId
    ? foundRel || {
      actorA: currentNpcId,
      actorB: partnerId,
      status: RelationshipStatus.Candidate,
      isOpen: false,
      statusHistory: [],
    }
    : undefined

  const sharedChildren = useMemo(() => {
    if (!currentNpcId || !partnerId) return []
    return Array.from(children.values()).filter((c) => {
      const isGuardianPair =
        (areSameFormId(c.guardianAId, currentNpcId) && areSameFormId(c.guardianBId, partnerId)) ||
        (areSameFormId(c.guardianAId, partnerId) && areSameFormId(c.guardianBId, currentNpcId))
      const isBioPair =
        (areSameFormId(c.bioMotherId, currentNpcId) && areSameFormId(c.bioFatherId, partnerId)) ||
        (areSameFormId(c.bioMotherId, partnerId) && areSameFormId(c.bioFatherId, currentNpcId))
      return isGuardianPair || isBioPair
    })
  }, [children, currentNpcId, partnerId])

  const targetNpcId = isPlayerPartner ? (currentNpcId !== PLAYER_FORM_ID ? currentNpcId : null) : partnerId
  const targetNpcProfile = targetNpcId ? npcProfiles.get(targetNpcId) : null
  const isTargetPlayer = !targetNpcId || targetNpcId === PLAYER_FORM_ID

  const affectionObj = targetNpcId ? affectionData.get(targetNpcId) : undefined
  const currentAffection = affectionObj?.permanent ?? 0
  const currentRank = targetNpcId ? hierarchyRanks.indexOf(targetNpcId) : -1
  const isMarriedToPlayer = !isTargetPlayer && currentRel?.status === RelationshipStatus.Married
  const isEngagedToPlayer = !isTargetPlayer && currentRel?.status === RelationshipStatus.Engaged
  const isLoverToPlayer = !isTargetPlayer && currentRel?.status === RelationshipStatus.Lover
  const isDatingToPlayer = !isTargetPlayer && currentRel?.status === RelationshipStatus.Dating
  const hasAffectionWithPlayer = !isTargetPlayer && (currentNpcId === PLAYER_FORM_ID || partnerId === PLAYER_FORM_ID) && (
    isMarriedToPlayer || isEngagedToPlayer || isLoverToPlayer || isDatingToPlayer
  )

  const availableHomes = [
    { value: 'Unset', label: 'Unset (No assigned house)' },
    ...playerHomes.map((h) => ({
      value: h.name,
      label: h.locationName ? `${h.name} (${h.locationName})` : h.name,
    })),
  ]

  const rankOptions = [
    { value: 'none', label: 'Unranked (4th+)' },
    { value: '0', label: '1st Rank Spouse' },
    { value: '1', label: '2nd Rank Spouse' },
    { value: '2', label: '3rd Rank Spouse' },
  ]

  // Partner's own categorized partners
  const partnerCategorized = partnerId && partnerProfile
    ? categorizePartners(partnerId, relationships, cheatingRecords, settings.enablePolygamy)
    : null

  const relevantCheatingRecords = useMemo(() => {
    if (!currentNpcId || !partnerId) return []
    const seen = new Set<string>()
    return cheatingRecords.filter((cr) => {
      // 1. Infidelity directly against each other (one is cheater, other is victim)
      const isDirectCheatingOnPartner =
        (areSameFormId(cr.cheaterId, currentNpcId) && areSameFormId(cr.victimId, partnerId)) ||
        (areSameFormId(cr.cheaterId, partnerId) && areSameFormId(cr.victimId, currentNpcId))

      // 2. Unofficial active affair (neither is official partner to each other, but having an ongoing affair)
      const isOfficial =
        currentRel &&
        (currentRel.status === RelationshipStatus.Married ||
          currentRel.status === RelationshipStatus.Engaged ||
          currentRel.status === RelationshipStatus.Dating ||
          currentRel.status === RelationshipStatus.Lover ||
          currentRel.status === RelationshipStatus.Divorced ||
          currentRel.status === RelationshipStatus.Jilted ||
          currentRel.status === RelationshipStatus.ExDating ||
          currentRel.status === RelationshipStatus.ExLover)

      const isOngoingAffairPartner =
        !isOfficial &&
        ((areSameFormId(cr.cheaterId, currentNpcId) && areSameFormId(cr.affairPartnerId, partnerId)) ||
          (areSameFormId(cr.cheaterId, partnerId) && areSameFormId(cr.affairPartnerId, currentNpcId))) &&
        (cr.affairStatus === undefined || cr.affairStatus === AffairStatus.Ongoing)

      if (!isDirectCheatingOnPartner && !isOngoingAffairPartner) {
        return false
      }

      const key = `${cr.cheaterId}:${cr.victimId}:${cr.affairPartnerId}:${cr.lastTimestamp}`
      if (seen.has(key)) return false
      seen.add(key)
      return true
    })
  }, [currentRel, cheatingRecords, currentNpcId, partnerId])

  const isUnofficialAffair = Boolean(
    relevantCheatingRecords.length > 0 &&
    (!currentRel || currentRel.status === RelationshipStatus.Unknown || currentRel.status === RelationshipStatus.Candidate)
  )

  return (
    <div
      className={`absolute top-0 right-0 bottom-0 z-30 w-[22rem] bg-panel border-l border-border-strong flex flex-col h-full overflow-hidden shadow-window transition-transform duration-300 ease-in-out ${isOpen ? 'translate-x-0' : 'translate-x-full pointer-events-none'
        }`}
    >
      {partnerProfile && (
        <>
          {/* Panel Header */}
          <header className="p-[0.875rem] bg-panel-raised border-b border-border-divider flex items-center justify-between">
            <div className="min-w-0 flex-1">
              <div className="flex items-center gap-[0.375rem]">
                {isPlayerPartner ? (
                  <Crown className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                ) : null}
                <h3 className="text-base font-bold text-accent-bright truncate">
                  {partnerProfile.name}
                </h3>
              </div>
              <div className="text-xs text-foreground-secondary flex items-center gap-[0.375rem] mt-[0.125rem]">
                <span className="font-mono text-foreground-muted">{partnerProfile.formId}</span>
                <span>•</span>
                <span>{partnerProfile.race}</span>
                {currentRel &&
                  currentRel.status !== RelationshipStatus.Unknown &&
                  currentRel.status !== RelationshipStatus.Candidate && (
                    <>
                      <span>•</span>
                      <StatusBadge status={currentRel.status} />
                    </>
                  )}
                {isUnofficialAffair && (
                  <>
                    <span>•</span>
                    <span className="text-[0.625rem] px-[0.375rem] py-[0.125rem] rounded bg-rose-500/20 text-rose-300 border border-rose-500/30 font-semibold flex items-center gap-[0.25rem]">
                      <Flame className="w-[0.875em] h-[0.875em]" />
                      <span>Affair Partner</span>
                    </span>
                  </>
                )}
              </div>
            </div>

            <AppTooltip content="Close Partner Panel">
              <button
                onClick={() => setPartnerId(null)}
                className="text-foreground-muted hover:text-accent p-[0.25rem] rounded hover:bg-accent-soft cursor-pointer transition-colors ml-[0.5rem]"
              >
                <X className="w-[1.125em] h-[1.125em]" />
              </button>
            </AppTooltip>
          </header>

          {/* Action Bar */}
          <div className="px-[0.875rem] py-[0.5rem] bg-panel-inset border-b border-border-divider flex justify-end">
            <button
              onClick={() => {
                setPartnerId(null)
                openNpcDetail(partnerProfile.formId)
              }}
              className="text-xs text-accent-bright hover:text-accent flex items-center gap-[0.25rem] font-medium cursor-pointer transition-colors"
            >
              <span>Open Full NPC Page</span>
              <ExternalLink className="w-[0.875em] h-[0.875em]" />
            </button>
          </div>

          {/* Content Sections */}
          <ScrollArea className="flex-1 p-[0.875rem]">
            <div className="space-y-[1rem] pr-[0.375rem]">
              {/* Relationship Status Selector */}
              {currentRel && currentNpcId && partnerId && (
                <SectionCard title="Relationship Status" icon={Heart}>
                  <div className="pt-[0.25rem] space-y-[0.5rem]">
                    <SelectInput
                      value={currentRel.status}
                      options={availableStatusOptions}
                      onChange={(val) =>
                        assignRelationshipStatus(currentNpcId, partnerId, parseInt(val) as RelationshipStatus)
                      }
                      className="w-full text-xs"
                    />
                    {(Boolean(
                      foundRel &&
                        foundRel.status !== RelationshipStatus.Candidate &&
                        foundRel.status !== RelationshipStatus.Unknown,
                    ) ||
                      isUnofficialAffair) && (
                      <AppTooltip content="Completely remove this relationship record, history, and affair records while keeping the NPC registered as a Candidate">
                        <button
                          onClick={() => removeRelationship(currentNpcId, partnerId)}
                          className="w-full flex items-center justify-center gap-[0.375rem] px-[0.625rem] py-[0.375rem] rounded text-xs font-medium bg-rose-950/40 text-status-danger hover:bg-rose-900/50 border border-rose-800/50 hover:border-rose-700/70 transition-colors cursor-pointer"
                        >
                          <Trash2 className="w-[0.95em] h-[0.95em]" />
                          <span>Remove Relationship Record</span>
                        </button>
                      </AppTooltip>
                    )}
                  </div>
                </SectionCard>
              )}

              {/* Affection Rating Block */}
              {hasAffectionWithPlayer && (
                <SectionCard title="Affection Rating" icon={Heart} subtitle={`${currentAffection}%`}>
                  <div className="space-y-[0.5rem] pt-[0.25rem]">
                    <SliderInput
                      value={currentAffection}
                      min={0}
                      max={100}
                      unit="%"
                      onChange={(val) => targetNpcId && updateAffection(targetNpcId, val)}
                      className="w-full"
                    />
                    <div className="flex items-center justify-between text-[0.6875rem] text-foreground-muted">
                      <span>Decay Mult: {affectionObj?.decayMultiplier ?? 1.0}x</span>
                      <span>
                        Last Seen:{' '}
                        {affectionObj?.daysSinceLastInteraction != null
                          ? Math.round(affectionObj.daysSinceLastInteraction) === 0
                            ? 'Today'
                            : `${Math.round(affectionObj.daysSinceLastInteraction)} days ago`
                          : 'Unknown'}
                      </span>
                    </div>
                  </div>
                </SectionCard>
              )}

              {/* Pair Open Relationship Toggle & Cheating Records */}
              {currentRel && (
                <SectionCard title="Relationship Policy" icon={Shield}>
                  <div className="text-xs pt-[0.25rem] space-y-[0.5rem]">
                    <div className="flex justify-between items-center">
                      <span className="text-foreground-muted font-medium">Pair Open Status:</span>
                      <button
                        onClick={() =>
                          useMarasStore.getState().setPairIsOpen(currentRel.actorA, currentRel.actorB, !currentRel.isOpen)
                        }
                        className={`px-[0.5rem] py-[0.25rem] rounded text-[0.6875rem] font-semibold border transition-all ${currentRel.isOpen
                          ? 'bg-emerald-500/20 text-emerald-400 border-emerald-500/40 hover:bg-emerald-500/30'
                          : 'bg-panel-raised text-foreground-muted border-border-divider hover:bg-interactive-hover'
                          }`}
                      >
                        {currentRel.isOpen ? 'Open Relationship' : 'Closed (Monogamous)'}
                      </button>
                    </div>
                    {(() => {
                      const records = relevantCheatingRecords
                      if (records.length === 0) return null
                      return (
                        <div className="pt-[0.375rem] border-t border-border-divider space-y-[0.25rem]">
                          <span className="text-destructive font-semibold block text-[0.6875rem]">Cheating / Affair Records:</span>
                          {records.map((cr, idx) => {
                            const cheaterName = cr.cheaterId === PLAYER_FORM_ID ? (npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)') : npcProfiles.get(cr.cheaterId)?.name || `NPC ${cr.cheaterId}`
                            const affairPartner = npcProfiles.get(cr.affairPartnerId)
                            const affairPartnerName = cr.affairPartnerId === PLAYER_FORM_ID ? (npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)') : affairPartner?.name || `NPC ${cr.affairPartnerId}`
                            const victimProfile = npcProfiles.get(cr.victimId)
                            const victimName = cr.victimId === PLAYER_FORM_ID ? (npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)') : victimProfile?.name || `NPC ${cr.victimId}`
                            const typeLabel = cr.affairType === AffairType.Lover ? 'Lover' : cr.affairType === AffairType.Dating ? 'Dating' : 'Casual'
                            const statusLabel =
                              cr.affairStatus === AffairStatus.Ongoing ? 'Ongoing' :
                                cr.affairStatus === AffairStatus.EndedBreakup ? 'Ended (Breakup)' :
                                  cr.affairStatus === AffairStatus.EndedVictimSplit ? 'Ended (Divorce/Split)' :
                                    cr.affairStatus === AffairStatus.Allowed ? 'Allowed (Open)' :
                                      cr.affairStatus === AffairStatus.EndedConfronted ? 'Ended (Confronted)' : 'Ongoing'
                            const isOngoing = cr.affairStatus === undefined || cr.affairStatus === AffairStatus.Ongoing

                            return (
                              <div key={idx} className="bg-destructive/10 text-destructive text-[0.6875rem] p-[0.375rem] rounded border border-destructive/20 flex justify-between items-center gap-[0.5rem]">
                                <div className="space-y-[0.125rem]">
                                  <div>
                                    <span>{cheaterName}'s affair with <span className="font-semibold text-foreground-primary">{affairPartnerName}</span>: {cr.count} incident(s) (Day {cr.lastTimestamp})</span>
                                  </div>
                                  {cr.victimId && cr.victimId !== currentNpcId && cr.victimId !== partnerId && (
                                    <div className="text-[0.625rem] text-foreground-muted">
                                      Cheating on: <span className="text-foreground-secondary font-medium">{victimName}</span>
                                    </div>
                                  )}
                                  <div className="flex items-center gap-[0.25rem]">
                                    <span className="px-[0.25rem] py-[0.0625rem] rounded bg-purple-500/20 text-purple-300 border border-purple-500/30 text-[0.5625rem] font-medium">
                                      {typeLabel}
                                    </span>
                                    <span className={`px-[0.25rem] py-[0.0625rem] rounded text-[0.5625rem] font-medium border ${isOngoing
                                      ? 'bg-red-500/20 text-red-300 border-red-500/30'
                                      : 'bg-zinc-500/20 text-zinc-300 border-zinc-500/30'
                                      }`}>
                                      {statusLabel}
                                    </span>
                                  </div>
                                </div>
                                <span className={`px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold flex-shrink-0 ${cr.isSecret
                                  ? 'bg-amber-500/20 text-amber-400 border border-amber-500/30'
                                  : 'bg-rose-500/20 text-rose-400 border border-rose-500/30'
                                  }`}>
                                  {cr.isSecret ? 'Secret' : 'Public'}
                                </span>
                              </div>
                            )
                          })}
                        </div>
                      )
                    })()}
                  </div>
                </SectionCard>
              )}

              {/* Children & Offspring Section */}
              {currentRel && (
                <SectionCard
                  title={`Children & Offspring (${sharedChildren.length})`}
                  icon={Baby}
                >
                  <div className="text-xs pt-[0.25rem] space-y-[0.5rem]">
                    {sharedChildren.length > 0 ? (
                      <div className="space-y-[0.375rem]">
                        {sharedChildren.map((child, idx) => {
                          const childProfile = npcProfiles.get(child.childId)
                          const isPlaceholderName = !childProfile?.name || childProfile.name.startsWith('NPC_')
                          const childName = (isPlaceholderName && child.name && !child.name.startsWith('NPC_'))
                            ? child.name
                            : (childProfile?.name || child.name || `Child ${child.childId}`)
                          const { statusLabel, badgeLabel, badgeStyle } = getChildRelationshipStatus(
                            child,
                            currentNpcId,
                            partnerId,
                          )

                          return (
                            <div
                              key={idx}
                              onClick={() => openNpcDetail(child.childId)}
                              className="p-[0.5rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded flex items-center justify-between transition-colors cursor-pointer"
                            >
                              <div className="flex items-center gap-[0.5rem]">
                                <Baby className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                                <div>
                                  <div className="font-semibold text-foreground-primary text-xs flex items-center gap-[0.375rem]">
                                    <span>{childName}</span>
                                    <span className="text-[0.625rem] font-mono text-foreground-muted">
                                      {child.childId}
                                    </span>
                                  </div>
                                  <span className="text-[0.6875rem] text-foreground-muted">
                                    {statusLabel}
                                    {child.timestamp != null && ` • ${formatGameDay(child.timestamp, currentGameDay)}`}
                                  </span>
                                </div>
                              </div>

                              <div className="flex items-center gap-[0.375rem]">
                                <span
                                  className={`px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold ${badgeStyle}`}
                                >
                                  {badgeLabel}
                                </span>
                                <AppTooltip content="Remove child">
                                  <button
                                    onClick={(e) => {
                                      e.stopPropagation()
                                      removeChild(child.childId)
                                    }}
                                    className="p-[0.25rem] text-foreground-muted hover:text-destructive hover:bg-destructive/10 rounded transition-colors"
                                  >
                                    <Trash2 className="w-[0.875em] h-[0.875em]" />
                                  </button>
                                </AppTooltip>
                              </div>
                            </div>
                          )
                        })}
                      </div>
                    ) : (
                      <p className="text-[0.6875rem] text-foreground-muted italic">
                        No children recorded for this relationship pair.
                      </p>
                    )}
                  </div>
                </SectionCard>
              )}

              {/* Partner's Other Relationships Summary */}
              {partnerCategorized && (
                <SectionCard title="Other Relationships" icon={Users}>
                  <div className="space-y-[0.375rem] text-xs pt-[0.25rem]">
                    {[
                      ...partnerCategorized.primary,
                      ...partnerCategorized.openRelationship,
                      ...partnerCategorized.cheating,
                    ]
                      .filter((r) => r.actorA !== currentNpcId && r.actorB !== currentNpcId)
                      .map((r, i) => {
                        const otherId = r.actorA === partnerId ? r.actorB : r.actorA
                        const otherName = otherId === PLAYER_FORM_ID ? (npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)') : npcProfiles.get(otherId)?.name || `NPC ${otherId}`
                        return (
                          <div
                            key={i}
                            className="flex justify-between items-center bg-panel p-[0.375rem] rounded border border-border-divider"
                          >
                            <span className="text-foreground-primary font-medium">{otherName}</span>
                            <span className="text-accent text-[0.6875rem] font-semibold">
                              {getStatusLabel(r.status)}
                            </span>
                          </div>
                        )
                      })}
                  </div>
                </SectionCard>
              )}
            </div>
          </ScrollArea>
        </>
      )}
    </div>
  )
}
