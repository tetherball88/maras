import React, { useState, useMemo, useEffect } from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { PLAYER_FORM_ID } from '../../../data/mockData'
import { Users, Lock, Search, X, RefreshCw, ExternalLink } from 'lucide-react'
import { areSameFormId } from '../../../utils/statusAvailability'
import { getParentRole } from '../../../utils/childUtils'
import { ChildRecord } from '../../../types'

interface ChildGuardiansSectionProps {
  npcId: string
  className?: string
}

type ParentSlotKey = 'guardianA' | 'guardianB' | 'bioMother' | 'bioFather' | 'presumedFather'

interface CandidateNpcOption {
  formId: string
  name: string
  race: string
  gender?: 'male' | 'female' | 'unknown' | 'undefined'
  pluginName?: string
}

const SLOT_META: Record<
  ParentSlotKey,
  {
    title: string
    filterLabel: string
    genderFilter?: 'male' | 'female'
  }
> = {
  guardianA: {
    title: 'Guardian A',
    filterLabel: 'All Adult NPCs',
  },
  guardianB: {
    title: 'Guardian B',
    filterLabel: 'All Adult NPCs',
  },
  bioMother: {
    title: 'Biological Mother',
    filterLabel: 'Females Only',
    genderFilter: 'female',
  },
  bioFather: {
    title: 'Biological Father',
    filterLabel: 'Males Only',
    genderFilter: 'male',
  },
  presumedFather: {
    title: 'Presumed Father',
    filterLabel: 'Males Only',
    genderFilter: 'male',
  },
}

export const ChildGuardiansSection: React.FC<ChildGuardiansSectionProps> = ({
  npcId,
  className = '',
}) => {
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const allSkyrimNpcs = useMarasStore((s) => s.allSkyrimNpcs)
  const isLoadingAllNpcs = useMarasStore((s) => s.isLoadingAllNpcs)
  const fetchAllSkyrimNpcs = useMarasStore((s) => s.fetchAllSkyrimNpcs)
  const children = useMarasStore((s) => s.children)
  const updateChild = useMarasStore((s) => s.updateChild)
  const setPaternitySecretEdges = useMarasStore((s) => s.setPaternitySecretEdges)
  const revealSecretsInUI = useMarasStore((s) => Boolean(s.settings.revealSecretsInUI))

  const [activeSlot, setActiveSlot] = useState<ParentSlotKey | null>(null)
  const [searchQuery, setSearchQuery] = useState('')

  useEffect(() => {
    setActiveSlot(null)
    setSearchQuery('')
  }, [npcId])

  const handleToggleSlotPicker = (slot: ParentSlotKey) => {
    if (activeSlot === slot) {
      setActiveSlot(null)
      setSearchQuery('')
      return
    }
    if (allSkyrimNpcs.length === 0 && !isLoadingAllNpcs) {
      fetchAllSkyrimNpcs()
    }
    setActiveSlot(slot)
    setSearchQuery('')
  }

  // Parent resolution for child NPCs
  const childRecord = children.get(npcId)

  const isSecretForPartner = Boolean(childRecord?.isSecretForPartner ?? childRecord?.isSecret)
  const isSecretForBio = Boolean(childRecord?.isSecretForBioFather)
  const hasActiveSecret = isSecretForPartner || isSecretForBio

  const handleToggleSecret = (type: 'partner' | 'bio', checked: boolean) => {
    if (!childRecord) return
    const currentPartner = isSecretForPartner
    const currentBio = isSecretForBio
    const newPartner = type === 'partner' ? checked : currentPartner
    const newBio = type === 'bio' ? checked : currentBio
    setPaternitySecretEdges(childRecord.childId, newBio, newPartner)
  }

  const playerName = npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
  const playerProfile = npcProfiles.get(PLAYER_FORM_ID)

  const cleanId = (id?: string) => (id && id !== '00000000' && id !== '00700000') ? id : undefined
  const gAId = cleanId(childRecord?.guardianAId)
  const gBId = cleanId(childRecord?.guardianBId)
  const bioMotherId = cleanId(childRecord?.bioMotherId)
  const bioFatherId = cleanId(childRecord?.bioFatherId)
  const presumedFatherId = cleanId(childRecord?.presumedFatherId)

  const resolveDisplayProfile = (id?: string, fallbackName?: string) => {
    if (id) {
      if (areSameFormId(id, PLAYER_FORM_ID)) {
        return {
          formId: PLAYER_FORM_ID,
          name: playerName,
          gender: playerProfile?.gender,
        }
      }
      const prof = npcProfiles.get(id)
      if (prof) return prof
      const compact = allSkyrimNpcs.find((n) => areSameFormId(n.formId, id))
      if (compact) {
        return {
          formId: compact.formId,
          name: compact.name,
          gender: compact.gender,
        }
      }
      return {
        formId: id,
        name: fallbackName || `NPC ${id}`,
        gender: undefined,
      }
    }
    if (fallbackName) {
      return {
        formId: '',
        name: fallbackName,
        gender: undefined,
      }
    }
    return null
  }

  const parentAProfile = resolveDisplayProfile(gAId, childRecord?.guardianAName)
  const parentBProfile = resolveDisplayProfile(gBId, childRecord?.guardianBName)
  const bioMotherProfile = resolveDisplayProfile(bioMotherId, childRecord?.bioMotherName)
  const bioFatherProfile = resolveDisplayProfile(bioFatherId, childRecord?.bioFatherName)
  const presumedFatherProfile = resolveDisplayProfile(presumedFatherId, childRecord?.presumedFatherName)
  const showPresumedFatherSlot = revealSecretsInUI || Boolean(presumedFatherProfile)

  useEffect(() => {
    if (!showPresumedFatherSlot && activeSlot === 'presumedFather') {
      setActiveSlot(null)
      setSearchQuery('')
    }
  }, [showPresumedFatherSlot, activeSlot])

  const getCurrentSlotFormId = (slot: ParentSlotKey): string | undefined => {
    switch (slot) {
      case 'guardianA':
        return gAId
      case 'guardianB':
        return gBId
      case 'bioMother':
        return bioMotherId
      case 'bioFather':
        return bioFatherId
      case 'presumedFather':
        return presumedFatherId
    }
  }

  const filteredCandidates = useMemo(() => {
    if (!activeSlot) return []
    const meta = SLOT_META[activeSlot]
    const seen = new Set<string>()
    const list: CandidateNpcOption[] = []

    const pushCandidate = (c: CandidateNpcOption, isChildFlag?: boolean) => {
      if (!c.formId || c.formId === '00000000' || areSameFormId(c.formId, npcId)) return
      if (isChildFlag || children.has(c.formId)) return
      const key = c.formId.toUpperCase()
      if (seen.has(key)) return
      if (meta.genderFilter && c.gender !== meta.genderFilter) return
      seen.add(key)
      list.push(c)
    }

    // Include Player first if gender matches
    pushCandidate({
      formId: PLAYER_FORM_ID,
      name: playerName,
      race: playerProfile?.race || 'Nord',
      gender: playerProfile?.gender || 'male',
      pluginName: 'Skyrim.esm',
    })

    for (const prof of npcProfiles.values()) {
      pushCandidate(
        {
          formId: prof.formId,
          name: prof.name,
          race: prof.race,
          gender: prof.gender,
          pluginName: prof.pluginName,
        },
        prof.isChild,
      )
    }

    for (const compact of allSkyrimNpcs) {
      pushCandidate(
        {
          formId: compact.formId,
          name: compact.name,
          race: compact.race,
          gender: compact.gender,
          pluginName: compact.pluginName,
        },
        compact.isChild,
      )
    }

    const q = searchQuery.trim().toLowerCase()
    const matched = q
      ? list.filter(
          (n) =>
            n.name.toLowerCase().includes(q) ||
            n.formId.toLowerCase().includes(q) ||
            (n.race && n.race.toLowerCase().includes(q)) ||
            (n.pluginName && n.pluginName.toLowerCase().includes(q)),
        )
      : list

    return matched.slice(0, 60)
  }, [activeSlot, npcProfiles, allSkyrimNpcs, children, npcId, playerName, playerProfile, searchQuery])

  const handleSelectCandidate = (slot: ParentSlotKey, candidate: CandidateNpcOption | null) => {
    if (!childRecord) return
    const nextId = candidate ? candidate.formId : ''
    const nextName = candidate ? candidate.name : ''
    const updates: Partial<ChildRecord> = {}

    if (slot === 'guardianA') {
      updates.guardianAId = nextId
      updates.guardianAName = nextName
    } else if (slot === 'guardianB') {
      updates.guardianBId = nextId
      updates.guardianBName = nextName
    } else if (slot === 'bioMother') {
      updates.bioMotherId = nextId
      updates.bioMotherName = nextName
      if (nextId) {
        updates.isAdopted = false
        updates.isBiologicalChild = true
      }
    } else if (slot === 'bioFather') {
      updates.bioFatherId = nextId
      updates.bioFatherName = nextName
      if (nextId) {
        updates.isAdopted = false
        updates.isBiologicalChild = true
      }
    } else if (slot === 'presumedFather') {
      updates.presumedFatherId = nextId
      updates.presumedFatherName = nextName
      if (nextId && (!childRecord.bioFatherId || !areSameFormId(nextId, childRecord.bioFatherId))) {
        updates.isSecret = true
        updates.isSecretForPartner = true
      } else if (!nextId) {
        updates.isSecret = false
        updates.isSecretForPartner = false
      }
    }

    updateChild(childRecord.childId, updates)
    setActiveSlot(null)
    setSearchQuery('')
  }

  return (
    <SectionCard title="Parents & Guardians" icon={Users} className={className}>
      <div className="pt-[0.25rem] space-y-[0.75rem] text-xs">
        {/* Guardians Row (2 slots) */}
        <div>
          <span className="text-[0.6875rem] font-medium text-foreground-muted block mb-[0.375rem]">
            Custodial Guardians (Click to Swap)
          </span>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-[0.75rem]">
            {([
              { slot: 'guardianA' as const, profile: parentAProfile, fallbackLabel: 'Guardian A' },
              { slot: 'guardianB' as const, profile: parentBProfile, fallbackLabel: 'Guardian B' },
            ]).map(({ slot, profile, fallbackLabel }) => {
              const isSelectedSlot = activeSlot === slot
              const isPlayerParent = Boolean(profile?.formId && areSameFormId(profile.formId, PLAYER_FORM_ID))
              const roleInfo = profile?.formId
                ? getParentRole(profile.formId, childRecord || {}, profile.gender)
                : { roleLabel: fallbackLabel, isSecret: false, isSecretForPartner: false, badgeStyle: 'text-foreground-muted' }

              return (
                <div
                  key={slot}
                  onClick={() => handleToggleSlotPicker(slot)}
                  className={`p-[0.75rem] bg-panel-inset border rounded flex items-center justify-between transition-colors cursor-pointer ${
                    isSelectedSlot
                      ? 'border-accent bg-interactive-hover'
                      : 'border-border-subtle hover:bg-interactive-hover hover:border-border-accent'
                  }`}
                >
                  <div className="min-w-0">
                    <div className="flex items-center gap-[0.375rem] mb-[0.125rem] flex-wrap">
                      <span className={`text-[0.6875rem] ${roleInfo.badgeStyle || 'text-foreground-muted'} block font-medium`}>
                        {roleInfo.roleLabel}
                      </span>
                      {roleInfo.isSecret && (
                        <span
                          className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-purple-500/20 text-purple-300 border border-purple-500/30 font-semibold uppercase tracking-wider"
                          title={roleInfo.isSecretForPartner ? 'Partner believes the child is theirs' : 'Secret from parent'}
                        >
                          {roleInfo.isSecretForPartner ? 'Secret from Partner' : 'Secret'}
                        </span>
                      )}
                    </div>
                    {profile ? (
                      <span className="font-bold text-accent-bright text-xs truncate block">{profile.name}</span>
                    ) : (
                      <span className="italic text-foreground-muted text-xs block">Unassigned — Click to assign</span>
                    )}
                  </div>
                  <div className="flex items-center gap-[0.375rem] flex-shrink-0 ml-[0.5rem]">
                    <span className="text-[0.625rem] text-foreground-muted flex items-center gap-[0.25rem]">
                      <RefreshCw className="w-[0.75em] h-[0.75em]" />
                      Swap
                    </span>
                    {profile?.formId && !isPlayerParent && (
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation()
                          openNpcDetail(profile.formId)
                        }}
                        className="px-[0.375rem] py-[0.125rem] rounded bg-panel-raised hover:bg-accent/20 text-[0.625rem] text-accent border border-border-subtle flex items-center gap-[0.25rem] transition-colors cursor-pointer"
                        title="Open NPC Profile"
                      >
                        <span>Profile</span>
                        <ExternalLink className="w-[0.75em] h-[0.75em]" />
                      </button>
                    )}
                  </div>
                </div>
              )
            })}
          </div>
        </div>

        {/* Biological (& Presumed when revealed) Parents */}
        <div className="pt-[0.5rem] border-t border-border-divider">
          <span className="text-[0.6875rem] font-medium text-foreground-muted block mb-[0.375rem]">
            {showPresumedFatherSlot
              ? 'Biological & Presumed Parents (Click to Swap)'
              : 'Biological Parents (Click to Swap)'}
          </span>
          <div className={`grid grid-cols-1 ${showPresumedFatherSlot ? 'sm:grid-cols-3' : 'sm:grid-cols-2'} gap-[0.5rem]`}>
            {/* Slot 3: Biological Mother (Females Only) */}
            {(() => {
              const isSelectedSlot = activeSlot === 'bioMother'
              const isPlayerBioMom = Boolean(bioMotherProfile?.formId && areSameFormId(bioMotherProfile.formId, PLAYER_FORM_ID))
              return (
                <div
                  onClick={() => handleToggleSlotPicker('bioMother')}
                  className={`p-[0.5rem] bg-panel-raised border rounded flex flex-col justify-between gap-[0.25rem] transition-colors cursor-pointer ${
                    isSelectedSlot
                      ? 'border-accent bg-interactive-hover'
                      : 'border-border-subtle hover:border-border-accent'
                  }`}
                >
                  <div className="flex items-center justify-between gap-[0.25rem]">
                    <span className="text-[0.625rem] text-accent font-medium">Biological Mother</span>
                    <div className="flex items-center gap-[0.25rem]">
                      <RefreshCw className="w-[0.75em] h-[0.75em] text-foreground-muted" />
                      {bioMotherProfile?.formId && !isPlayerBioMom && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation()
                            openNpcDetail(bioMotherProfile.formId)
                          }}
                          className="p-[0.125rem] rounded hover:bg-accent/20 text-accent transition-colors cursor-pointer"
                          title="Open Biological Mother Profile"
                        >
                          <ExternalLink className="w-[0.75em] h-[0.75em]" />
                        </button>
                      )}
                    </div>
                  </div>
                  {bioMotherProfile ? (
                    <span className="text-foreground-primary font-medium truncate">{bioMotherProfile.name}</span>
                  ) : (
                    <span className="text-foreground-muted italic text-[0.6875rem]">Unassigned (♀)</span>
                  )}
                </div>
              )
            })()}

            {/* Slot 4: Biological Father (Males Only) */}
            {(() => {
              const isSelectedSlot = activeSlot === 'bioFather'
              const isPlayerBioDad = Boolean(bioFatherProfile?.formId && areSameFormId(bioFatherProfile.formId, PLAYER_FORM_ID))
              return (
                <div
                  onClick={() => handleToggleSlotPicker('bioFather')}
                  className={`p-[0.5rem] bg-panel-raised border rounded flex flex-col justify-between gap-[0.25rem] transition-colors cursor-pointer ${
                    isSelectedSlot
                      ? 'border-accent bg-interactive-hover'
                      : 'border-border-subtle hover:border-border-accent'
                  }`}
                >
                  <div className="flex items-center justify-between gap-[0.25rem] flex-wrap">
                    <div className="flex items-center gap-[0.25rem]">
                      <span className="text-[0.625rem] text-accent font-medium">Biological Father</span>
                      {childRecord?.isSecretForBioFather && (
                        <span
                          className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-indigo-500/20 text-indigo-300 border border-indigo-500/30 font-semibold uppercase tracking-wider"
                          title="Biological father is unaware that he fathered this child"
                        >
                          Secret From Him
                        </span>
                      )}
                    </div>
                    <div className="flex items-center gap-[0.25rem]">
                      <RefreshCw className="w-[0.75em] h-[0.75em] text-foreground-muted" />
                      {bioFatherProfile?.formId && !isPlayerBioDad && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation()
                            openNpcDetail(bioFatherProfile.formId)
                          }}
                          className="p-[0.125rem] rounded hover:bg-accent/20 text-accent transition-colors cursor-pointer"
                          title="Open Biological Father Profile"
                        >
                          <ExternalLink className="w-[0.75em] h-[0.75em]" />
                        </button>
                      )}
                    </div>
                  </div>
                  {bioFatherProfile ? (
                    <span className="text-foreground-primary font-medium truncate">{bioFatherProfile.name}</span>
                  ) : (
                    <span className="text-foreground-muted italic text-[0.6875rem]">Unassigned (♂)</span>
                  )}
                </div>
              )
            })()}

            {/* Slot 5: Presumed Father (Males Only) */}
            {showPresumedFatherSlot && (() => {
              const isSelectedSlot = activeSlot === 'presumedFather'
              const isPlayerPresumedDad = Boolean(presumedFatherProfile?.formId && areSameFormId(presumedFatherProfile.formId, PLAYER_FORM_ID))
              return (
                <div
                  onClick={() => handleToggleSlotPicker('presumedFather')}
                  className={`p-[0.5rem] bg-panel-raised border rounded flex flex-col justify-between gap-[0.25rem] transition-colors cursor-pointer ${
                    isSelectedSlot
                      ? 'border-accent bg-interactive-hover'
                      : 'border-border-subtle hover:border-border-accent'
                  }`}
                >
                  <div className="flex items-center justify-between gap-[0.25rem] flex-wrap">
                    <div className="flex items-center gap-[0.25rem]">
                      <span className="text-[0.625rem] text-purple-300 font-medium">Presumed Father</span>
                      {presumedFatherProfile && (
                        isSecretForPartner ? (
                          <span
                            className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-purple-500/20 text-purple-300 border border-purple-500/30 font-semibold uppercase tracking-wider"
                            title="Partner believes the child is his biological child"
                          >
                            Secret From Partner
                          </span>
                        ) : childRecord?.discoveredByPartner ? (
                          <span
                            className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-sky-500/20 text-sky-300 border border-sky-500/30 font-semibold uppercase tracking-wider"
                            title="Partner has discovered the true paternity"
                          >
                            Truth Known
                          </span>
                        ) : null
                      )}
                    </div>
                    <div className="flex items-center gap-[0.25rem]">
                      <RefreshCw className="w-[0.75em] h-[0.75em] text-foreground-muted" />
                      {presumedFatherProfile?.formId && !isPlayerPresumedDad && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation()
                            openNpcDetail(presumedFatherProfile.formId)
                          }}
                          className="p-[0.125rem] rounded hover:bg-accent/20 text-accent transition-colors cursor-pointer"
                          title="Open Presumed Father Profile"
                        >
                          <ExternalLink className="w-[0.75em] h-[0.75em]" />
                        </button>
                      )}
                    </div>
                  </div>
                  {presumedFatherProfile ? (
                    <span className="text-foreground-primary font-medium truncate">{presumedFatherProfile.name}</span>
                  ) : (
                    <span className="text-foreground-muted italic text-[0.6875rem]">None / Unassigned (♂)</span>
                  )}
                </div>
              )
            })()}
          </div>
        </div>

        {/* Inline NPC Picker Panel when any of the 5 slots is clicked */}
        {activeSlot && (
          <div className="p-[0.625rem] bg-panel-inset border border-accent/40 rounded space-y-[0.5rem]">
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-[0.375rem]">
                <span className="text-xs font-bold text-accent-bright">
                  Swap {SLOT_META[activeSlot].title}
                </span>
                <span className="text-[0.625rem] px-[0.375rem] py-[0.0625rem] rounded bg-accent/15 text-accent border border-accent/30 font-medium">
                  {SLOT_META[activeSlot].filterLabel}
                </span>
              </div>
              <button
                type="button"
                onClick={() => {
                  setActiveSlot(null)
                  setSearchQuery('')
                }}
                className="p-[0.25rem] rounded text-foreground-muted hover:text-foreground-primary hover:bg-panel-raised transition-colors cursor-pointer"
                title="Close picker"
              >
                <X className="w-[0.875em] h-[0.875em]" />
              </button>
            </div>

            <div className="relative">
              <Search className="w-[0.875em] h-[0.875em] text-foreground-muted absolute left-[0.5rem] top-1/2 -translate-y-1/2 pointer-events-none" />
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                placeholder={`Search ${SLOT_META[activeSlot].filterLabel.toLowerCase()} by name, race, plugin, or FormID...`}
                autoFocus
                className="w-full pl-[1.75rem] pr-[0.5rem] py-[0.375rem] bg-panel-raised border border-border-subtle rounded text-xs text-foreground-primary placeholder:text-foreground-muted focus:outline-none focus:border-accent"
              />
            </div>

            <div className="max-h-[12rem] overflow-y-auto space-y-[0.25rem] pr-[0.25rem]">
              {getCurrentSlotFormId(activeSlot) && (
                <button
                  type="button"
                  onClick={() => handleSelectCandidate(activeSlot, null)}
                  className="w-full px-[0.5rem] py-[0.375rem] rounded bg-rose-950/30 hover:bg-rose-950/50 border border-rose-500/30 text-rose-300 text-left text-xs font-medium flex items-center justify-between transition-colors cursor-pointer"
                >
                  <span>Clear / Unassign {SLOT_META[activeSlot].title}</span>
                  <span className="text-[0.625rem] uppercase">None</span>
                </button>
              )}

              {isLoadingAllNpcs && filteredCandidates.length === 0 ? (
                <div className="text-foreground-muted italic py-[0.5rem] text-center">
                  Loading NPCs...
                </div>
              ) : filteredCandidates.length === 0 ? (
                <div className="text-foreground-muted italic py-[0.5rem] text-center">
                  No matching NPCs found.
                </div>
              ) : (
                filteredCandidates.map((cand) => {
                  const currentId = getCurrentSlotFormId(activeSlot)
                  const isCurrent = Boolean(currentId && areSameFormId(currentId, cand.formId))
                  const isPlayer = areSameFormId(cand.formId, PLAYER_FORM_ID)
                  const genderSym = cand.gender === 'female' ? '♀' : cand.gender === 'male' ? '♂' : ''

                  return (
                    <button
                      type="button"
                      key={cand.formId}
                      onClick={() => handleSelectCandidate(activeSlot, cand)}
                      className={`w-full px-[0.5rem] py-[0.375rem] rounded border text-left flex items-center justify-between transition-colors cursor-pointer ${
                        isCurrent
                          ? 'bg-accent/20 border-accent text-accent-bright'
                          : 'bg-panel-raised hover:bg-interactive-hover border-border-subtle text-foreground-primary'
                      }`}
                    >
                      <div className="flex items-center gap-[0.375rem] min-w-0">
                        {genderSym && (
                          <span className="text-[0.6875rem] text-foreground-muted flex-shrink-0">{genderSym}</span>
                        )}
                        <span className="font-medium truncate">{cand.name}</span>
                        {isPlayer && (
                          <span className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-amber-500/20 text-amber-300 border border-amber-500/30 font-semibold uppercase flex-shrink-0">
                            Player
                          </span>
                        )}
                        {isCurrent && (
                          <span className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-accent/30 text-accent-bright font-semibold uppercase flex-shrink-0">
                            Current
                          </span>
                        )}
                      </div>
                      <div className="flex items-center gap-[0.375rem] text-[0.625rem] text-foreground-muted flex-shrink-0 ml-[0.5rem]">
                        {cand.race && <span>{cand.race}</span>}
                        <span className="font-mono">{cand.formId}</span>
                      </div>
                    </button>
                  )
                })
              )}
            </div>
          </div>
        )}

        {/* Paternity Secrecy Manual Controls */}
        {hasActiveSecret && (
          <div className="pt-[0.625rem] border-t border-border-divider space-y-[0.5rem]">
            <div className="flex items-center justify-between">
              <span className="text-[0.6875rem] font-semibold text-foreground-muted uppercase tracking-wider flex items-center gap-[0.375rem]">
                <Lock className="w-[0.875em] h-[0.875em] text-purple-400" />
                Paternity Secrecy Controls
              </span>
              <span className="text-[0.625rem] text-foreground-muted">Manual Control</span>
            </div>

            <div className={isSecretForPartner && isSecretForBio ? "grid grid-cols-1 sm:grid-cols-2 gap-[0.5rem]" : "grid grid-cols-1 gap-[0.5rem]"}>
              {/* Secret from Partner */}
              {isSecretForPartner && (
                <label className="flex items-start gap-[0.625rem] p-[0.625rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded cursor-pointer transition-colors select-none group">
                  <input
                    type="checkbox"
                    checked={Boolean(childRecord?.isSecretForPartner ?? childRecord?.isSecret)}
                    onChange={(e) => handleToggleSecret('partner', e.target.checked)}
                    className="mt-[0.125rem] h-4 w-4 rounded border-border-divider text-purple-500 focus:ring-purple-500/20 accent-purple-500 cursor-pointer flex-shrink-0"
                  />
                  <div className="text-left space-y-[0.125rem]">
                    <span className="text-xs font-semibold text-foreground-primary group-hover:text-purple-300 transition-colors block">
                      Secret from Partner
                    </span>
                    <span className="text-[0.6875rem] text-foreground-muted block leading-tight">
                      Partner believes the child is theirs; true biological parentage is hidden from them.
                    </span>
                  </div>
                </label>
              )}

              {/* Secret from Biological Father */}
              {isSecretForBio && (
                <label className="flex items-start gap-[0.625rem] p-[0.625rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded cursor-pointer transition-colors select-none group">
                  <input
                    type="checkbox"
                    checked={Boolean(childRecord?.isSecretForBioFather)}
                    onChange={(e) => handleToggleSecret('bio', e.target.checked)}
                    className="mt-[0.125rem] h-4 w-4 rounded border-border-divider text-indigo-500 focus:ring-indigo-500/20 accent-indigo-500 cursor-pointer flex-shrink-0"
                  />
                  <div className="text-left space-y-[0.125rem]">
                    <span className="text-xs font-semibold text-foreground-primary group-hover:text-indigo-300 transition-colors block">
                      Secret from Biological Father
                    </span>
                    <span className="text-[0.6875rem] text-foreground-muted block leading-tight">
                      Biological father is unaware that he fathered this child.
                    </span>
                  </div>
                </label>
              )}
            </div>
          </div>
        )}
      </div>
    </SectionCard>
  )
}
