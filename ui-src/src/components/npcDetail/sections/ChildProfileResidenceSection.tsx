import React, { useState } from 'react'
import { useTranslation } from '../../../i18n'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import { ChildRelocationModal } from '../ChildRelocationModal'
import { ChildTrainingModal } from '../ChildTrainingModal'
import { PLAYER_FORM_ID } from '../../../data/mockData'
import { Baby, Home, GraduationCap, Swords, AlertTriangle, UserPlus, CheckCircle2 } from 'lucide-react'
import { formatGameDay } from '../../../utils/dateUtils'
import { areSameFormId } from '../../../utils/statusAvailability'
import { resolveIsFemale } from '../../../utils/childUtils'
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
  const { t, tp } = useTranslation()
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
  const spouseName = spouseProfile?.name || (isSpouse ? t('npcDetail.childResidence.fallbackSpouse') : null)

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
    (c) =>
      npcProfiles.get(c.childId)?.name ||
      c.name ||
      t('store.fallbackChildName', { childId: c.childId }),
  )

  // Relocation modal state
  const [isModalOpen, setIsModalOpen] = useState(false)
  const [pendingHome, setPendingHome] = useState<string | null>(null)

  const currentHomeValue = npc?.currentHome || 'Unset'
  const isCurrentlyUnset = !npc?.currentHome || npc?.currentHome === 'Unset'
  const hasSpouseCoParent = Boolean(isSpouse && coParentId)

  const availableHomes = [
    ...(hasSpouseCoParent && !isCurrentlyUnset
      ? []
      : [{ value: 'Unset', label: t('npcDetail.childResidence.unsetOriginalLocation') }]),
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
    ? t('npcDetail.childResidence.unsetOriginalLocation')
    : playerHomes.find((h) => h.name === pendingHome)?.locationName
      ? `${pendingHome} (${playerHomes.find((h) => h.name === pendingHome)?.locationName})`
      : pendingHome || ''

  const expectedLabel =
    isFemale === true
      ? t('npcDetail.childResidence.statusExpectedDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.statusExpectedSon')
        : t('npcDetail.childResidence.statusExpectedChild')

  const infantLabel =
    isFemale === true
      ? t('npcDetail.childResidence.statusInfantDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.statusInfantSon')
        : t('npcDetail.childResidence.statusInfantChild')

  const inTrainingBaseLabel =
    isFemale === true
      ? t('npcDetail.childResidence.statusInTrainingDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.statusInTrainingSon')
        : t('npcDetail.childResidence.statusInTrainingChild')

  const adultBaseLabel = hasRefID
    ? isFemale === true
      ? t('npcDetail.childResidence.statusAdultDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.statusAdultSon')
        : t('npcDetail.childResidence.statusAdultChild')
    : isFemale === true
      ? t('npcDetail.childResidence.statusAdultReadyDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.statusAdultReadySon')
        : t('npcDetail.childResidence.statusAdultReadyChild')

  const unspawnedStatusLabel =
    isFemale === true
      ? t('npcDetail.childResidence.statusUnspawnedDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.statusUnspawnedSon')
        : t('npcDetail.childResidence.statusUnspawnedChild')

  const adoptedStatusLabel =
    isFemale === true
      ? t('utils.child.adoptedDaughter')
      : isFemale === false
        ? t('utils.child.adoptedSon')
        : t('utils.child.adoptedChild')

  const biologicalStatusLabel =
    isFemale === true
      ? t('utils.child.biologicalDaughter')
      : isFemale === false
        ? t('utils.child.biologicalSon')
        : t('utils.child.biologicalChild')

  const unspawnedYoungTitle =
    isFemale === true
      ? t('npcDetail.childResidence.unspawnedYoungDaughter')
      : isFemale === false
        ? t('npcDetail.childResidence.unspawnedYoungSon')
        : t('npcDetail.childResidence.unspawnedYoungChild')

  return (
    <>
      <SectionCard title={t('npcDetail.childResidence.title')} icon={Baby} className={className}>
        <div className="pt-[0.25rem] space-y-[0.75rem] text-xs">
          <div className="grid grid-cols-2 gap-[0.75rem]">
            <div>
              <span className="text-foreground-muted block mb-[0.125rem]">
                {t('npcDetail.childResidence.fullName')}
              </span>
              <span className="font-semibold text-foreground-primary text-sm">{displayName}</span>
            </div>

            <div>
              <span className="text-foreground-muted block mb-[0.125rem]">
                {t('npcDetail.childResidence.race')}
              </span>
              <span className="font-semibold text-foreground-primary">{displayRace}</span>
            </div>
          </div>

          <div className="grid grid-cols-2 gap-[0.75rem] pt-[0.5rem] border-t border-border-divider">
            <div>
              <span className="text-foreground-muted block mb-[0.125rem]">
                {t('npcDetail.childResidence.status')}
              </span>
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
                    <span>{expectedLabel}</span>
                  </>
                ) : stage === 'infant' ? (
                  <>
                    <Baby className="w-[0.875em] h-[0.875em]" />
                    <span>{infantLabel}</span>
                  </>
                ) : stage === 'training' ? (
                  <>
                    <GraduationCap className="w-[0.875em] h-[0.875em]" />
                    <span>
                      {inTrainingBaseLabel}
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
                      {adultBaseLabel}
                      {(childRecord?.trainingClassName || npc?.trainingClassName)
                        ? ` — ${childRecord?.trainingClassName || npc?.trainingClassName}`
                        : ''}
                    </span>
                  </>
                ) : !hasRefID ? (
                  <>
                    <span>{unspawnedStatusLabel}</span>
                  </>
                ) : !childRecord ? (
                  t('npcDetail.childResidence.eligibleForAdoption')
                ) : childRecord.isAdopted !== false ? (
                  adoptedStatusLabel
                ) : (
                  biologicalStatusLabel
                )}
                {stage === 'unborn' ? (
                  childRecord?.conceptionTimestamp != null && childRecord.conceptionTimestamp > 0
                    ? ` ${t('npcDetail.childResidence.conceivedSuffix', {
                        date: formatGameDay(childRecord.conceptionTimestamp, currentGameDay),
                      })}`
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
                  title={t('npcDetail.childResidence.beastbloodInheritedTooltip')}
                >
                  <span>{t('npcDetail.shell.badgeBeastblood')}</span>
                </span>
              )}
            </div>

            {hasSpouseCoParent && spouseName && (
              <div>
                <span className="text-foreground-muted block mb-[0.125rem]">
                  {t('npcDetail.childResidence.householdCoParent')}
                </span>
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
                  <div className="font-semibold text-xs text-pink-200">
                    {t('npcDetail.childResidence.inGestationTitle')}
                  </div>
                  <div className="text-[0.6875rem] text-pink-200/80 mt-[0.0625rem]">
                    {t('npcDetail.childResidence.inGestationDesc')}
                  </div>
                </div>
              </div>
            ) : stage === 'infant' ? (
              <div className="p-[0.625rem] bg-cyan-500/10 border border-cyan-500/30 rounded flex items-center gap-[0.625rem] text-cyan-300">
                <Baby className="w-[1.25em] h-[1.25em] flex-shrink-0 text-cyan-400" />
                <div>
                  <div className="font-semibold text-xs text-cyan-200">
                    {t('npcDetail.childResidence.swaddledInfantTitle')}
                  </div>
                  <div className="text-[0.6875rem] text-cyan-200/80 mt-[0.0625rem]">
                    {t('npcDetail.childResidence.swaddledInfantDesc')}
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
                const trainClass =
                  childRecord?.trainingClassName ||
                  npc?.trainingClassName ||
                  t('npcDetail.childResidence.defaultTrainingClass')
                const trainLoc =
                  childRecord?.trainingLocation ||
                  npc?.trainingLocation ||
                  t('npcDetail.childResidence.defaultTrainingLocation')
                return (
                  <div className="p-[0.75rem] bg-purple-500/10 border border-purple-500/30 rounded space-y-[0.5rem] text-purple-300">
                    <div className="flex items-start justify-between gap-[0.5rem]">
                      <div className="flex items-center gap-[0.5rem]">
                        <GraduationCap className="w-[1.25em] h-[1.25em] flex-shrink-0 text-purple-400" />
                        <div>
                          <div className="font-semibold text-xs text-purple-200">
                            {t('npcDetail.childResidence.trainingAsAt', {
                              className: trainClass,
                              location: trainLoc,
                            })}
                          </div>
                          <div className="text-[0.6875rem] text-purple-200/80 mt-[0.0625rem]">
                            {remainingDays > 0
                              ? tp('npcDetail.childResidence.trainingDaysRemaining', remainingDays, {
                                  elapsed: Math.floor(elapsedDays),
                                  duration: durationDays,
                                })
                              : tp('npcDetail.childResidence.trainingCompletedDuration', durationDays)}
                          </div>
                        </div>
                      </div>
                      <button
                        onClick={() => completeChildTraining(npcId)}
                        className="text-xs font-semibold text-purple-100 bg-purple-600/30 hover:bg-purple-600/45 border border-purple-400/50 px-[0.625rem] py-[0.3125rem] rounded transition-colors flex items-center gap-[0.3125rem] cursor-pointer flex-shrink-0"
                      >
                        <CheckCircle2 className="w-[0.95em] h-[0.95em] text-purple-300" />
                        <span>{t('npcDetail.childResidence.completeTrainingEarly')}</span>
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
                      {childRecord?.trainingClassName || npc?.trainingClassName
                        ? t('npcDetail.childResidence.trainingCompleteReadyWithClass', {
                            className: childRecord?.trainingClassName || npc?.trainingClassName,
                          })
                        : t('npcDetail.childResidence.trainingCompleteReady')}
                    </div>
                    <div className="text-[0.6875rem] text-amber-200/80 mt-[0.0625rem]">
                      {t('npcDetail.childResidence.trainingCompleteDesc')}
                    </div>
                  </div>
                </div>
                <button
                  onClick={() => spawnChild(npcId)}
                  className="text-xs font-semibold text-amber-100 bg-amber-600/30 hover:bg-amber-600/45 border border-amber-400/50 px-[0.75rem] py-[0.375rem] rounded transition-colors flex items-center gap-[0.375rem] cursor-pointer flex-shrink-0"
                >
                  <Swords className="w-[1em] h-[1em] text-amber-300" />
                  <span>{t('npcDetail.shell.spawnAdult')}</span>
                </button>
              </div>
            ) : stage === 'adult' ? (
              <div className="p-[0.625rem] bg-amber-500/10 border border-amber-500/30 rounded flex items-center gap-[0.625rem] text-amber-300">
                <Swords className="w-[1.25em] h-[1.25em] flex-shrink-0 text-amber-400" />
                <div>
                  <div className="font-semibold text-xs text-amber-200">
                    {t('npcDetail.childResidence.independentAdventurerTitle', {
                      location: npc?.currentHome || t('npcDetail.childResidence.travelingSkyrim'),
                    })}
                  </div>
                  <div className="text-[0.6875rem] text-amber-200/80 mt-[0.0625rem]">
                    {t('npcDetail.childResidence.independentAdventurerDesc')}
                  </div>
                </div>
              </div>
            ) : !hasRefID ? (
              <div className="p-[0.75rem] bg-emerald-500/10 border border-emerald-500/30 rounded flex items-center justify-between gap-[0.75rem] text-emerald-300">
                <div className="flex items-center gap-[0.625rem]">
                  <AlertTriangle className="w-[1.25em] h-[1.25em] flex-shrink-0 text-emerald-400" />
                  <div>
                    <div className="font-semibold text-xs text-emerald-200">{unspawnedYoungTitle}</div>
                    <div className="text-[0.6875rem] text-emerald-200/80 mt-[0.0625rem]">
                      {t('npcDetail.childResidence.unspawnedYoungDesc')}
                    </div>
                  </div>
                </div>
                <button
                  onClick={() => spawnChild(npcId)}
                  className="text-xs font-semibold text-emerald-100 bg-emerald-600/30 hover:bg-emerald-600/45 border border-emerald-400/50 px-[0.75rem] py-[0.375rem] rounded transition-colors flex items-center gap-[0.375rem] cursor-pointer flex-shrink-0"
                >
                  <UserPlus className="w-[1em] h-[1em] text-emerald-300" />
                  <span>{t('npcDetail.shell.spawnChild')}</span>
                </button>
              </div>
            ) : isPlayerChildResolved && hasSpouseCoParent ? (
              <div>
                <span className="text-foreground-muted block mb-[0.125rem]">
                  {t('npcDetail.childResidence.assignedResidence')}
                </span>
                <span className="font-medium text-accent-bright flex items-center gap-[0.25rem]">
                  <Home className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>
                    {npc?.currentHome ||
                      spouseProfile?.currentHome ||
                      t('npcDetail.childResidence.unassigned')}
                  </span>
                </span>
                <p className="text-[0.625rem] text-foreground-muted mt-[0.25rem]">
                  {t('npcDetail.childResidence.controlledByCoParent', { spouseName })}
                </p>
              </div>
            ) : isPlayerChildResolved ? (
              <>
                <label className="text-foreground-muted block mb-[0.125rem] font-medium flex items-center gap-[0.25rem]">
                  <Home className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>{t('npcDetail.childResidence.assignedResidence')}</span>
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
                <span className="text-foreground-muted block mb-[0.125rem]">
                  {t('npcDetail.childResidence.assignedHome')}
                </span>
                <span className="font-medium text-accent-bright flex items-center gap-[0.25rem]">
                  <Home className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>
                    {npc?.currentHome ||
                      npc?.originalHome ||
                      (childRecord?.guardianAId && childRecord.guardianAId !== '00000000'
                        ? npcProfiles.get(childRecord.guardianAId)?.currentHome ||
                          npcProfiles.get(childRecord.guardianAId)?.originalHome
                        : undefined) ||
                      t('npcDetail.childResidence.followingCustodialParent')}
                  </span>
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

            const isEntryUnlocked = (item: TrainingCatalogItem): boolean => {
              if (item.requirementMet !== undefined) {
                return Boolean(item.requirementMet)
              }
              if (item.factionKey && playerFactions) {
                return Boolean(playerFactions[item.factionKey])
              }
              return true
            }

            return (
              <div className="pt-[0.625rem] border-t border-border-divider space-y-[0.5rem]">
                <div className="flex items-center justify-between gap-[0.75rem]">
                  <div>
                    <span className="font-semibold text-foreground-primary flex items-center gap-[0.375rem]">
                      <GraduationCap className="w-[1em] h-[1em] text-purple-400" />
                      <span>{t('npcDetail.childResidence.trainingSectionTitle')}</span>
                    </span>
                    <p className="text-[0.6875rem] text-foreground-muted mt-[0.125rem]">
                      {tp('npcDetail.childResidence.trainingEnrollDesc', durationDays, {
                        name: displayName,
                        catalogCount: trainingCatalog.length,
                      })}
                    </p>
                  </div>
                  <button
                    onClick={() => setIsTrainingModalOpen(true)}
                    className="text-xs font-semibold px-[0.875rem] py-[0.4375rem] rounded border transition-colors flex items-center gap-[0.375rem] flex-shrink-0 text-purple-100 bg-purple-600/25 hover:bg-purple-600/40 border-purple-500/45 cursor-pointer"
                  >
                    <GraduationCap className="w-[0.95em] h-[0.95em] text-purple-300" />
                    <span>
                      {hasRefID
                        ? t('npcDetail.childResidence.despawnAndTrainBtn')
                        : t('npcDetail.trainingModal.sendToTrainingBtn')}
                    </span>
                  </button>
                </div>

                {hasRefID && (
                  <p className="text-[0.625rem] text-foreground-muted">
                    {tp('npcDetail.childResidence.despawnTrainingHelp', durationDays)}
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

