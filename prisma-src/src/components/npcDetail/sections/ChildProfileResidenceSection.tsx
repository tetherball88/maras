import React, { useState } from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import { ChildRelocationModal } from '../ChildRelocationModal'
import { ChildTrainingModal } from '../ChildTrainingModal'
import { PLAYER_FORM_ID } from '../../../data/mockData'
import { Baby, Home, GraduationCap, Swords, AlertTriangle, UserPlus, CheckCircle2 } from 'lucide-react'
import { formatGameDay } from '../../../utils/dateUtils'
import { areSameFormId } from '../../../utils/statusAvailability'
import { resolveIsFemale, getChildGenderNouns } from '../../../utils/childUtils'
import { RelationshipStatus, ChildStage, TrainingCatalogItem } from '../../../types'

interface ChildProfileResidenceSectionProps {
  npcId: string
  displayName: string
  displayRace: string
  isPlayerChild?: boolean
  className?: string
}

export const ChildProfileResidenceSection: React.FC<ChildProfileResidenceSectionProps> = ({
  npcId,
  displayName,
  displayRace,
  isPlayerChild,
  className = '',
}) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const children = useMarasStore((s) => s.children)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)
  const playerHomes = useMarasStore((s) => s.playerHomes)
  const settings = useMarasStore((s) => s.settings)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const relocateFamilyUnit = useMarasStore((s) => s.relocateFamilyUnit)
  const spawnChild = useMarasStore((s) => s.spawnChild)
  const sendChildToTraining = useMarasStore((s) => s.sendChildToTraining)
  const completeChildTraining = useMarasStore((s) => s.completeChildTraining)

  const [isTrainingModalOpen, setIsTrainingModalOpen] = useState<boolean>(false)

  const npc = npcProfiles.get(npcId)

  // Parent resolution for child NPCs
  const childRecord = children.get(npcId)

  const stage: ChildStage = childRecord?.stage || npc?.stage || 'child'
  const hasRefID = childRecord?.hasRefID ?? npc?.hasRefID ?? !npcId.startsWith('FF7')
  const hasBeastblood = Boolean(childRecord?.hasBeastblood || npc?.hasBeastblood)
  const isFemale = resolveIsFemale(childRecord, npc)
  const { sonOrDaughter } = getChildGenderNouns(isFemale)

  const isPlayerParent = childRecord && (areSameFormId(childRecord.guardianAId, PLAYER_FORM_ID) || areSameFormId(childRecord.guardianBId, PLAYER_FORM_ID))
  const isPlayerChildResolved = isPlayerChild !== undefined ? isPlayerChild : Boolean(isPlayerParent)
  const coParentId = isPlayerParent
    ? (areSameFormId(childRecord.guardianAId, PLAYER_FORM_ID) ? childRecord.guardianBId : childRecord.guardianAId)
    : null

  const isSpouse = coParentId
    ? relationships.some(
      (r) =>
        r.status === RelationshipStatus.Married &&
        ((areSameFormId(r.actorA, PLAYER_FORM_ID) && areSameFormId(r.actorB, coParentId)) ||
          (areSameFormId(r.actorB, PLAYER_FORM_ID) && areSameFormId(r.actorA, coParentId))),
    )
    : false

  const spouseProfile = isSpouse && coParentId ? npcProfiles.get(coParentId) : null
  const spouseName = spouseProfile?.name || (isSpouse ? 'Spouse' : null)

  // Other children sharing this spouse / relationship
  const uniqueOtherChildren = (isSpouse && coParentId)
    ? Array.from(children.values()).filter(
      (c) =>
        !areSameFormId(c.childId, npcId) &&
        ((areSameFormId(c.guardianAId, PLAYER_FORM_ID) && areSameFormId(c.guardianBId, coParentId)) ||
          (areSameFormId(c.guardianBId, PLAYER_FORM_ID) && areSameFormId(c.guardianAId, coParentId))),
    )
    : []

  const otherChildrenNames = uniqueOtherChildren.map(
    (c) => npcProfiles.get(c.childId)?.name || c.name || `Child ${c.childId}`,
  )

  // Relocation modal state
  const [isModalOpen, setIsModalOpen] = useState(false)
  const [pendingHome, setPendingHome] = useState<string | null>(null)

  const currentHomeValue = npc?.currentHome || 'Unset'
  const isCurrentlyUnset = !npc?.currentHome || npc?.currentHome === 'Unset'
  const hasSpouseCoParent = Boolean(isSpouse && coParentId)

  const availableHomes = [
    ...(hasSpouseCoParent && !isCurrentlyUnset ? [] : [{ value: 'Unset', label: 'Unset (Original Location)' }]),
    ...playerHomes.map((h) => ({
      value: h.name,
      label: h.locationName ? `${h.name} (${h.locationName})` : h.name,
    })),
  ]

  const handleSelectHome = (newHome: string) => {
    if (newHome === currentHomeValue) return

    if (hasSpouseCoParent || uniqueOtherChildren.length > 0) {
      setPendingHome(newHome)
      setIsModalOpen(true)
    } else {
      const targetVal = newHome === 'Unset' ? undefined : newHome
      updateNPCProfile(npcId, { currentHome: targetVal })
    }
  }

  const handleConfirmRelocation = () => {
    if (!pendingHome) return
    const targetVal = pendingHome === 'Unset' ? undefined : pendingHome

    relocateFamilyUnit({
      childId: npcId,
      home: targetVal || '',
      coParentId: hasSpouseCoParent && coParentId ? coParentId : undefined,
      siblingIds: uniqueOtherChildren.map((c) => c.childId),
    })

    setIsModalOpen(false)
    setPendingHome(null)
  }

  const handleCloseModal = () => {
    setIsModalOpen(false)
    setPendingHome(null)
  }

  const targetHomeLabel = pendingHome === 'Unset'
    ? 'Unset (Original Location)'
    : playerHomes.find((h) => h.name === pendingHome)?.locationName
      ? `${pendingHome} (${playerHomes.find((h) => h.name === pendingHome)?.locationName})`
      : pendingHome || ''

  return (
    <>
      <SectionCard title="Child Profile & Residence" icon={Baby} className={className}>
        <div className="pt-[0.25rem] space-y-[0.75rem] text-xs">
          <div className="grid grid-cols-2 gap-[0.75rem]">
            <div>
              <span className="text-foreground-muted block mb-[0.125rem]">Full Name</span>
              <span className="font-semibold text-foreground-primary text-sm">{displayName}</span>
            </div>

            <div>
              <span className="text-foreground-muted block mb-[0.125rem]">Race</span>
              <span className="font-semibold text-foreground-primary">{displayRace}</span>
            </div>
          </div>

          <div className="grid grid-cols-2 gap-[0.75rem] pt-[0.5rem] border-t border-border-divider">
            <div>
              <span className="text-foreground-muted block mb-[0.125rem]">Status</span>
              <span
                className={`inline-flex items-center gap-[0.25rem] px-[0.375rem] py-[0.125rem] rounded text-[0.6875rem] font-bold ${stage === 'unborn'
                  ? 'bg-pink-500/20 text-pink-300 border border-pink-500/30'
                  : stage === 'infant'
                    ? 'bg-cyan-500/20 text-cyan-300 border border-cyan-500/30'
                    : stage === 'training'
                      ? 'bg-purple-500/20 text-purple-300 border border-purple-500/30'
                      : stage === 'adult'
                        ? 'bg-amber-500/20 text-amber-300 border border-amber-500/30'
                        : !hasRefID
                          ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30'
                          : !childRecord
                            ? 'bg-status-success-soft text-status-success border border-status-success'
                            : childRecord.isAdopted !== false
                              ? 'bg-amber-500/20 text-amber-300 border border-amber-500/30'
                              : 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30'
                  }`}
              >
                {stage === 'unborn' ? (
                  <>
                    <span>🤰 Expected {sonOrDaughter} (In Gestation)</span>
                  </>
                ) : stage === 'infant' ? (
                  <>
                    <Baby className="w-[0.875em] h-[0.875em]" />
                    <span>Newborn Infant {sonOrDaughter}</span>
                  </>
                ) : stage === 'training' ? (
                  <>
                    <GraduationCap className="w-[0.875em] h-[0.875em]" />
                    <span>
                      In Training ({sonOrDaughter})
                      {(childRecord?.trainingClassName || npc?.trainingClassName)
                        ? ` — ${childRecord?.trainingClassName || npc?.trainingClassName}`
                        : ''}
                      {(childRecord?.trainingLocation || npc?.trainingLocation)
                        ? ` (${childRecord?.trainingLocation || npc?.trainingLocation})`
                        : ''}
                    </span>
                  </>
                ) : stage === 'adult' ? (
                  <>
                    <Swords className="w-[0.875em] h-[0.875em]" />
                    <span>
                      {hasRefID ? `Adult ${sonOrDaughter}` : `Adult ${sonOrDaughter} (Ready to Spawn)`}
                      {(childRecord?.trainingClassName || npc?.trainingClassName)
                        ? ` — ${childRecord?.trainingClassName || npc?.trainingClassName}`
                        : ''}
                    </span>
                  </>
                ) : !hasRefID ? (
                  <>
                    <span>🧸 {sonOrDaughter} (Unspawned Record)</span>
                  </>
                ) : !childRecord ? (
                  'Eligible for Adoption'
                ) : childRecord.isAdopted !== false ? (
                  `Adopted ${sonOrDaughter}`
                ) : (
                  `Biological ${sonOrDaughter}`
                )}
                {stage === 'unborn' ? (
                  childRecord?.conceptionTimestamp != null && childRecord.conceptionTimestamp > 0
                    ? ` (Conceived ${formatGameDay(childRecord.conceptionTimestamp, currentGameDay)})`
                    : ''
                ) : (
                  childRecord?.timestamp != null && childRecord.timestamp > 0
                    ? ` (${formatGameDay(childRecord.timestamp, currentGameDay)})`
                    : ''
                )}
              </span>
              {hasBeastblood && (
                <span
                  className="inline-flex items-center gap-[0.25rem] ml-[0.375rem] px-[0.375rem] py-[0.125rem] rounded text-[0.6875rem] font-bold bg-amber-600/20 text-amber-200 border border-amber-500/40"
                  title="Carries Hircine's Beastblood (inherited from a werewolf biological parent)"
                >
                  <span>🐺 Beastblood</span>
                </span>
              )}
            </div>

            {hasSpouseCoParent && spouseName && (
              <div>
                <span className="text-foreground-muted block mb-[0.125rem]">Household Co-Parent</span>
                <span className="font-semibold text-accent-bright">{spouseName}</span>
              </div>
            )}
          </div>

          {/* Residence Section - Contextual based on developmental stage */}
          <div className="pt-[0.5rem] border-t border-border-divider space-y-[0.25rem]">
            {stage === 'unborn' ? (
              <div className="p-[0.625rem] bg-pink-500/10 border border-pink-500/30 rounded flex items-center gap-[0.625rem] text-pink-300">
                <span className="text-base flex-shrink-0">🤰</span>
                <div>
                  <div className="font-semibold text-xs text-pink-200">In Gestation (Womb Care)</div>
                  <div className="text-[0.6875rem] text-pink-200/80 mt-[0.0625rem]">
                    This child is currently developing in maternal gestation. Household assignment will be configured upon birth.
                  </div>
                </div>
              </div>
            ) : stage === 'infant' ? (
              <div className="p-[0.625rem] bg-cyan-500/10 border border-cyan-500/30 rounded flex items-center gap-[0.625rem] text-cyan-300">
                <Baby className="w-[1.25em] h-[1.25em] flex-shrink-0 text-cyan-400" />
                <div>
                  <div className="font-semibold text-xs text-cyan-200">Swaddled Newborn Infant</div>
                  <div className="text-[0.6875rem] text-cyan-200/80 mt-[0.0625rem]">
                    This infant is currently carried and swaddled in maternal care. A physical household bed and sandbox assignment will become available once the child grows into a small child.
                  </div>
                </div>
              </div>
            ) : stage === 'training' ? (
              (() => {
                const durationDays = Math.max(1, settings.childTrainingDurationDays ?? 14)
                const startDay = childRecord?.trainingStartGameDay ?? npc?.trainingStartGameDay ?? currentGameDay
                const elapsedDays = Math.max(0, currentGameDay - startDay)
                const progressPct = Math.min(100, Math.round((elapsedDays / durationDays) * 100))
                const remainingDays = Math.max(0, Math.ceil(durationDays - elapsedDays))
                const trainClass = childRecord?.trainingClassName || npc?.trainingClassName || 'Warrior'
                const trainLoc = childRecord?.trainingLocation || npc?.trainingLocation || 'Skyrim Academy'
                return (
                  <div className="p-[0.75rem] bg-purple-500/10 border border-purple-500/30 rounded space-y-[0.5rem] text-purple-300">
                    <div className="flex items-start justify-between gap-[0.5rem]">
                      <div className="flex items-center gap-[0.5rem]">
                        <GraduationCap className="w-[1.25em] h-[1.25em] flex-shrink-0 text-purple-400" />
                        <div>
                          <div className="font-semibold text-xs text-purple-200">
                            Training as {trainClass} at {trainLoc}
                          </div>
                          <div className="text-[0.6875rem] text-purple-200/80 mt-[0.0625rem]">
                            {remainingDays > 0
                              ? `${remainingDays} day(s) remaining (${Math.floor(elapsedDays)} / ${durationDays} days)`
                              : `Training duration completed (${durationDays} days)`}
                          </div>
                        </div>
                      </div>
                      <button
                        onClick={() => completeChildTraining(npcId)}
                        className="text-xs font-semibold text-purple-100 bg-purple-600/30 hover:bg-purple-600/45 border border-purple-400/50 px-[0.625rem] py-[0.3125rem] rounded transition-colors flex items-center gap-[0.3125rem] cursor-pointer flex-shrink-0"
                      >
                        <CheckCircle2 className="w-[0.95em] h-[0.95em] text-purple-300" />
                        <span>Complete Training Early</span>
                      </button>
                    </div>
                    <div className="w-full h-[0.375rem] bg-panel-inset rounded-full overflow-hidden border border-purple-500/20">
                      <div
                        className="h-full bg-purple-500 transition-transform origin-left"
                        style={{ transform: `scaleX(${progressPct / 100})` }}
                      />
                    </div>
                  </div>
                )
              })()
            ) : stage === 'adult' && !hasRefID ? (
              <div className="p-[0.75rem] bg-amber-500/10 border border-amber-500/30 rounded flex items-center justify-between gap-[0.75rem] text-amber-300">
                <div className="flex items-center gap-[0.625rem]">
                  <Swords className="w-[1.25em] h-[1.25em] flex-shrink-0 text-amber-400" />
                  <div>
                    <div className="font-semibold text-xs text-amber-200">
                      Training Complete — Ready to Spawn Adult {(childRecord?.trainingClassName || npc?.trainingClassName) ? `(${childRecord?.trainingClassName || npc?.trainingClassName})` : ''}
                    </div>
                    <div className="text-[0.6875rem] text-amber-200/80 mt-[0.0625rem]">
                      This offspring has completed their training and reached adulthood. Spawn their adult actor to send them out into Skyrim as an independent traveling adventurer available for hire.
                    </div>
                  </div>
                </div>
                <button
                  onClick={() => spawnChild(npcId)}
                  className="text-xs font-semibold text-amber-100 bg-amber-600/30 hover:bg-amber-600/45 border border-amber-400/50 px-[0.75rem] py-[0.375rem] rounded transition-colors flex items-center gap-[0.375rem] cursor-pointer flex-shrink-0"
                >
                  <Swords className="w-[1em] h-[1em] text-amber-300" />
                  <span>Spawn Adult</span>
                </button>
              </div>
            ) : stage === 'adult' ? (
              <div className="p-[0.625rem] bg-amber-500/10 border border-amber-500/30 rounded flex items-center gap-[0.625rem] text-amber-300">
                <Swords className="w-[1.25em] h-[1.25em] flex-shrink-0 text-amber-400" />
                <div>
                  <div className="font-semibold text-xs text-amber-200">
                    Independent Traveling Adventurer — {npc?.currentHome || 'Traveling Skyrim'}
                  </div>
                  <div className="text-[0.6875rem] text-amber-200/80 mt-[0.0625rem]">
                    Travels independently between Skyrim&apos;s inns, cities, and family homes, and is available to be hired as a follower.
                  </div>
                </div>
              </div>
            ) : !hasRefID ? (
              <div className="p-[0.75rem] bg-emerald-500/10 border border-emerald-500/30 rounded flex items-center justify-between gap-[0.75rem] text-emerald-300">
                <div className="flex items-center gap-[0.625rem]">
                  <AlertTriangle className="w-[1.25em] h-[1.25em] flex-shrink-0 text-emerald-400" />
                  <div>
                    <div className="font-semibold text-xs text-emerald-200">Unspawned Young {sonOrDaughter}</div>
                    <div className="text-[0.6875rem] text-emerald-200/80 mt-[0.0625rem]">
                      Spawn this young child into the world to assign a household bed and AI sandbox, or send them directly to adolescent training below.
                    </div>
                  </div>
                </div>
                <button
                  onClick={() => spawnChild(npcId)}
                  className="text-xs font-semibold text-emerald-100 bg-emerald-600/30 hover:bg-emerald-600/45 border border-emerald-400/50 px-[0.75rem] py-[0.375rem] rounded transition-colors flex items-center gap-[0.375rem] cursor-pointer flex-shrink-0"
                >
                  <UserPlus className="w-[1em] h-[1em] text-emerald-300" />
                  <span>Spawn Child</span>
                </button>
              </div>
            ) : isPlayerChildResolved && hasSpouseCoParent ? (
              <div>
                <span className="text-foreground-muted block mb-[0.125rem]">Assigned Residence</span>
                <span className="font-medium text-accent-bright flex items-center gap-[0.25rem]">
                  <Home className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>{npc?.currentHome || spouseProfile?.currentHome || 'Unassigned'}</span>
                </span>
                <p className="text-[0.625rem] text-foreground-muted mt-[0.25rem]">
                  Residence is controlled by co-parent ({spouseName})&apos;s assigned home.
                </p>
              </div>
            ) : isPlayerChildResolved ? (
              <>
                <label className="text-foreground-muted block mb-[0.125rem] font-medium flex items-center gap-[0.25rem]">
                  <Home className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>Assigned Residence</span>
                </label>
                <SelectInput
                  value={currentHomeValue}
                  options={availableHomes}
                  onChange={handleSelectHome}
                  className="w-full text-xs"
                />
              </>
            ) : (
              <div>
                <span className="text-foreground-muted block mb-[0.125rem]">Assigned Home</span>
                <span className="font-medium text-accent-bright flex items-center gap-[0.25rem]">
                  <Home className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>{npc?.currentHome || npc?.originalHome || (childRecord?.guardianAId && childRecord.guardianAId !== '00000000' ? (npcProfiles.get(childRecord.guardianAId)?.currentHome || npcProfiles.get(childRecord.guardianAId)?.originalHome) : undefined) || 'Following Custodial Parent'}</span>
                </span>
              </div>
            )}
          </div>

          {/* Adolescent Training Initiation — Available for both unspawned and physically spawned Stage 2 children */}
          {stage === 'child' && childRecord && (() => {
            const trainingCatalog: TrainingCatalogItem[] = settings.trainingCatalog ?? []
            const requireFaction = (settings.childTrainingRequireFaction ?? true) && isPlayerChildResolved
            const playerFactions = settings.playerTrainingFactions
            const baseCost = isPlayerChildResolved ? Math.max(0, settings.childTrainingCost ?? 2000) : 0
            const playerGold = playerFactions?.playerGold ?? 999999
            const durationDays = Math.max(1, settings.childTrainingDurationDays ?? 14)

            const isEntryUnlocked = (t: TrainingCatalogItem): boolean => {
              if (t.requirementMet !== undefined) {
                return Boolean(t.requirementMet)
              }
              if (t.factionKey && playerFactions) {
                return Boolean(playerFactions[t.factionKey])
              }
              return true
            }

            return (
              <div className="pt-[0.625rem] border-t border-border-divider space-y-[0.5rem]">
                <div className="flex items-center justify-between gap-[0.75rem]">
                  <div>
                    <span className="font-semibold text-foreground-primary flex items-center gap-[0.375rem]">
                      <GraduationCap className="w-[1em] h-[1em] text-purple-400" />
                      <span>Adolescent Training &amp; Class Path</span>
                    </span>
                    <p className="text-[0.6875rem] text-foreground-muted mt-[0.125rem]">
                      Enroll {displayName} in one of {trainingCatalog.length} academies, guilds, or Daedric covenants ({durationDays} day curriculum).
                    </p>
                  </div>
                  <button
                    onClick={() => setIsTrainingModalOpen(true)}
                    className="text-xs font-semibold px-[0.875rem] py-[0.4375rem] rounded border transition-colors flex items-center gap-[0.375rem] flex-shrink-0 text-purple-100 bg-purple-600/25 hover:bg-purple-600/40 border-purple-500/45 cursor-pointer"
                  >
                    <GraduationCap className="w-[0.95em] h-[0.95em] text-purple-300" />
                    <span>{hasRefID ? 'Despawn Child & Send to Training' : 'Send to Training'}</span>
                  </button>
                </div>

                {hasRefID && (
                  <p className="text-[0.625rem] text-foreground-muted">
                    Sending a spawned child to training will despawn their young child actor, preserve their SkyrimNet childhood biography, and allow you to spawn their adult actor after {durationDays} day(s).
                  </p>
                )}

                <ChildTrainingModal
                  isOpen={isTrainingModalOpen}
                  onClose={() => setIsTrainingModalOpen(false)}
                  onSelectTraining={(classIdx) => sendChildToTraining(npcId, classIdx)}
                  childName={displayName}
                  hasRefID={hasRefID}
                  hasBeastblood={hasBeastblood}
                  trainingDiscounts={childRecord?.trainingDiscounts}
                  isPlayerChild={isPlayerChildResolved}
                  durationDays={durationDays}
                  baseCost={baseCost}
                  playerGold={playerGold}
                  requireFaction={requireFaction}
                  trainingCatalog={trainingCatalog}
                  isEntryUnlocked={isEntryUnlocked}
                />
              </div>
            )
          })()}
        </div>
      </SectionCard>

      {/* Relocation Modal */}
      <ChildRelocationModal
        isOpen={isModalOpen}
        onClose={handleCloseModal}
        onConfirm={handleConfirmRelocation}
        childName={displayName}
        targetHomeName={targetHomeLabel}
        spouseName={hasSpouseCoParent ? spouseName : null}
        otherChildrenNames={otherChildrenNames}
      />
    </>
  )
}

