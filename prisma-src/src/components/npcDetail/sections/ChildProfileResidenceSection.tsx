import React, { useState } from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import { ChildRelocationModal } from '../ChildRelocationModal'
import { PLAYER_FORM_ID } from '../../../data/mockData'
import { Baby, Home, GraduationCap, Swords, AlertTriangle } from 'lucide-react'
import { formatGameDay } from '../../../utils/dateUtils'
import { areSameFormId } from '../../../utils/statusAvailability'
import { resolveIsFemale, getChildGenderNouns } from '../../../utils/childUtils'
import { RelationshipStatus, ChildStage } from '../../../types'

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
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const relocateFamilyUnit = useMarasStore((s) => s.relocateFamilyUnit)

  const npc = npcProfiles.get(npcId)

  // Parent resolution for child NPCs
  const childRecord = children.get(npcId)

  const stage: ChildStage = childRecord?.stage || npc?.stage || 'child'
  const hasRefID = childRecord?.hasRefID ?? npc?.hasRefID ?? !npcId.startsWith('FF7')
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
                    <span>In Training ({sonOrDaughter}) {childRecord?.trainingLocation ? `(${childRecord.trainingLocation})` : ''}</span>
                  </>
                ) : stage === 'adult' ? (
                  <>
                    <Swords className="w-[0.875em] h-[0.875em]" />
                    <span>{hasRefID ? `Adult Follower (${sonOrDaughter})` : `Adult ${sonOrDaughter} (Unsummoned)`}</span>
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
              <div className="p-[0.625rem] bg-purple-500/10 border border-purple-500/30 rounded flex items-center gap-[0.625rem] text-purple-300">
                <GraduationCap className="w-[1.25em] h-[1.25em] flex-shrink-0 text-purple-400" />
                <div>
                  <div className="font-semibold text-xs text-purple-200">Undergoing Apprenticeship / Training</div>
                  <div className="text-[0.6875rem] text-purple-200/80 mt-[0.0625rem]">
                    Currently stationed away at <strong className="text-purple-100">{childRecord?.trainingLocation || 'Skyrim Academy'}</strong>. Household assignment is paused until training concludes.
                  </div>
                </div>
              </div>
            ) : stage === 'adult' && !hasRefID ? (
              <div className="p-[0.625rem] bg-amber-500/10 border border-amber-500/30 rounded flex items-center gap-[0.625rem] text-amber-300">
                <Swords className="w-[1.25em] h-[1.25em] flex-shrink-0 text-amber-400" />
                <div>
                  <div className="font-semibold text-xs text-amber-200">Adult Follower Awaiting Summon</div>
                  <div className="text-[0.6875rem] text-amber-200/80 mt-[0.0625rem]">
                    This offspring has completed their training and reached adulthood. Summon them via follower dialogue or the MCM to bring their physical 3D actor into the world.
                  </div>
                </div>
              </div>
            ) : !hasRefID ? (
              <div className="p-[0.625rem] bg-amber-500/10 border border-amber-500/30 rounded flex items-center gap-[0.625rem] text-amber-300">
                <AlertTriangle className="w-[1.25em] h-[1.25em] flex-shrink-0 text-amber-400" />
                <div>
                  <div className="font-semibold text-xs text-amber-200">Unspawned {sonOrDaughter} Dependent</div>
                  <div className="text-[0.6875rem] text-amber-200/80 mt-[0.0625rem]">
                    This child is registered in the family record. Once adopted or spawned as an active Hearthfire kid, an in-game residence can be designated.
                  </div>
                </div>
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

