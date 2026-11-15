import React, { useState, useMemo } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus, PlayerHouse } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { SearchInput } from '../shared/SearchInput'
import { AppTooltip } from '../shared/AppTooltip'
import { HouseSelectionModal, HouseOccupantInfo } from './HouseSelectionModal'
import { ScrollArea } from '@/components/ui/scroll-area'
import {
  Baby,
  User,
  Check,
  Home,
  Users,
  ShieldCheck,
  Heart,
  ExternalLink,
  PlusCircle,
  AlertCircle,
} from 'lucide-react'

import { formatGameDay } from '../../utils/dateUtils'
import { resolveIsFemale, getChildGenderNouns } from '../../utils/childUtils'

export const AdoptionSubPage: React.FC = () => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const childrenStore = useMarasStore((s) => s.children)
  const relationships = useMarasStore((s) => s.relationships)
  const playerHomes = useMarasStore((s) => s.playerHomes)
  const addChild = useMarasStore((s) => s.addChild)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)

  const [searchChildQuery, setSearchChildQuery] = useState('')
  const [filterStatus, setFilterStatus] = useState<'all' | 'available' | 'adopted' | 'biological'>('all')
  const [searchParentQuery, setSearchParentQuery] = useState('')

  const [houseModalState, setHouseModalState] = useState<{
    isOpen: boolean
    childId: string
    childName: string
    actorA: string
    actorB: string | null
    spouseName: string | null
    assignSpouseHome: boolean
  } | null>(null)

  // Combine all children from npcProfiles and childrenStore
  const childMap = new Map<string, any>()
  npcProfiles.forEach((npc) => {
    if (npc.isChild) childMap.set(npc.formId, npc)
  })
  childrenStore.forEach((c) => {
    if (!childMap.has(c.childId)) {
      const profileFromMap = npcProfiles.get(c.childId)
      childMap.set(c.childId, profileFromMap || {
        formId: c.childId,
        name: c.name || `Child ${c.childId}`,
        race: 'Nord',
        isChild: true,
      })
    }
  })
  const allChildren = Array.from(childMap.values())

  const getActorName = (formId?: string | null): string | null => {
    if (!formId) return null
    if (formId === PLAYER_FORM_ID) return npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
    return npcProfiles.get(formId)?.name || null
  }

  // Map each child to their current adoption status
  const childrenWithAdoptionInfo = allChildren.map((child) => {
    const childRecord = childrenStore.get(child.formId)
    const parentAId = childRecord?.guardianAId || childRecord?.bioMotherId
    const parentBId = childRecord?.guardianBId || childRecord?.bioFatherId
    const parentAName = getActorName(parentAId)
    const parentBName = getActorName(parentBId)

    const isAssigned = Boolean(childRecord && (parentAName || parentBName))
    const isAdopted = isAssigned && childRecord?.isAdopted !== false
    const isBiological = isAssigned && childRecord?.isAdopted === false
    const isUnassigned = !isAssigned
    const blockedReason = child.blockedFromAdoptionReason || null
    const isBlocked = isUnassigned && Boolean(blockedReason)

    const bioParentName =
      childRecord?.bioMotherName ||
      getActorName(childRecord?.bioMotherId) ||
      childRecord?.bioFatherName ||
      getActorName(childRecord?.bioFatherId) ||
      null

    return {
      profile: child,
      isAssigned,
      isAdopted,
      isBiological,
      isUnassigned,
      isBlocked,
      blockedReason,
      childRecord,
      parentAName,
      parentBName,
      bioParentName,
    }
  })

  // Filter children based on status and search query
  const filteredChildren = childrenWithAdoptionInfo.filter((item) => {
    if (filterStatus === 'available' && (!item.isUnassigned || item.isBlocked)) return false
    if (filterStatus === 'adopted' && !item.isAdopted) return false
    if (filterStatus === 'biological' && !item.isBiological) return false

    if (searchChildQuery.trim()) {
      const q = searchChildQuery.toLowerCase()
      const nameMatch = item.profile.name.toLowerCase().includes(q)
      const formIdMatch = item.profile.formId.toLowerCase().includes(q)
      const raceMatch = item.profile.race.toLowerCase().includes(q)
      const homeMatch = (item.profile.currentHome || '').toLowerCase().includes(q)
      const parentMatch =
        (item.parentAName || '').toLowerCase().includes(q) ||
        (item.parentBName || '').toLowerCase().includes(q)
      const bioMatch = (item.bioParentName || '').toLowerCase().includes(q)
      return nameMatch || formIdMatch || raceMatch || homeMatch || parentMatch || bioMatch
    }

    return true
  })

  // Currently selected child ID
  const defaultSelectedChildId = filteredChildren.length > 0 ? filteredChildren[0].profile.formId : (allChildren[0]?.formId || '00003B8E')
  const [selectedChildId, setSelectedChildId] = useState<string>(defaultSelectedChildId)

  // Selected child object info
  const selectedChildInfo = childrenWithAdoptionInfo.find((item) => item.profile.formId === selectedChildId) || filteredChildren[0] || childrenWithAdoptionInfo[0]

  // Find all spouses of the player (RelationshipStatus.Married)
  const playerSpouses = useMemo(() => {
    return relationships
      .filter(
        (r) =>
          r.status === RelationshipStatus.Married &&
          (r.actorA === PLAYER_FORM_ID || r.actorB === PLAYER_FORM_ID)
      )
      .map((r) => {
        const spouseId = r.actorA === PLAYER_FORM_ID ? r.actorB : r.actorA
        const prof = npcProfiles.get(spouseId)
        return {
          formId: spouseId,
          name: prof?.name || getActorName(spouseId),
          race: prof?.race || 'Nord',
          pluginName: prof?.pluginName,
          currentHome: prof?.currentHome,
          stayAtHome: prof?.stayAtHome,
        }
      })
  }, [relationships, npcProfiles])

  // Compute occupant counters for each player house
  const housesWithOccupants: HouseOccupantInfo[] = useMemo(() => {
    return playerHomes.map((house) => {
      let childrenCount = 0
      let spousesCount = 0

      npcProfiles.forEach((npc) => {
        if (!npc.currentHome) return
        const matches =
          npc.currentHome === house.name ||
          npc.currentHome.toLowerCase() === house.name.toLowerCase() ||
          npc.currentHome === house.formId

        if (matches) {
          if (npc.isChild) {
            childrenCount++
          } else {
            const isPlayerSpouse = playerSpouses.some((sp) => sp.formId === npc.formId)
            if (isPlayerSpouse) {
              spousesCount++
            }
          }
        }
      })

      return {
        ...house,
        childrenCount,
        spousesCount,
      }
    })
  }, [playerHomes, npcProfiles, playerSpouses])

  // Gather other adult candidate NPCs (excluding player, who is handled with combinations)
  const otherAdultMap = new Map<string, { formId: string; name: string; race: string; pluginName?: string }>()

  npcProfiles.forEach((npc) => {
    if (!npc.isChild && npc.formId !== PLAYER_FORM_ID) {
      otherAdultMap.set(npc.formId, {
        formId: npc.formId,
        name: npc.name || `NPC ${npc.formId}`,
        race: npc.race || 'Nord',
        pluginName: npc.pluginName,
      })
    }
  })

  relationships.forEach((r) => {
    if (r.actorA && r.actorA !== PLAYER_FORM_ID && !otherAdultMap.has(r.actorA) && !npcProfiles.get(r.actorA)?.isChild) {
      const prof = npcProfiles.get(r.actorA)
      otherAdultMap.set(r.actorA, {
        formId: r.actorA,
        name: getActorName(r.actorA) || r.actorA,
        race: prof?.race || 'Nord',
        pluginName: prof?.pluginName,
      })
    }
    if (r.actorB && r.actorB !== PLAYER_FORM_ID && !otherAdultMap.has(r.actorB) && !npcProfiles.get(r.actorB)?.isChild) {
      const prof = npcProfiles.get(r.actorB)
      otherAdultMap.set(r.actorB, {
        formId: r.actorB,
        name: getActorName(r.actorB) || r.actorB,
        race: prof?.race || 'Nord',
        pluginName: prof?.pluginName,
      })
    }
  })

  interface AdoptiveCandidateItem {
    key: string
    formId: string
    name: string
    race: string
    pluginName?: string
    partnerId: string | null
    partnerName: string | null
    spouseHome?: string
    isPlayer: boolean
    badgeLabel?: string
  }

  const allAdultCandidates: AdoptiveCandidateItem[] = []

  const playerDisplayName = npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
  const playerRace = npcProfiles.get(PLAYER_FORM_ID)?.race || 'Nord'

  // 1. Add Player options
  if (playerSpouses.length === 0) {
    allAdultCandidates.push({
      key: PLAYER_FORM_ID,
      formId: PLAYER_FORM_ID,
      name: playerDisplayName,
      race: playerRace,
      pluginName: 'Skyrim.esm',
      partnerId: null,
      partnerName: null,
      isPlayer: true,
      badgeLabel: 'Single Parent',
    })
  } else {
    // Player as Single Parent
    if (playerSpouses.length == 0) {
      allAdultCandidates.push({
        key: `${PLAYER_FORM_ID}_single`,
        formId: PLAYER_FORM_ID,
        name: playerDisplayName,
        race: playerRace,
        pluginName: 'Skyrim.esm',
        partnerId: null,
        partnerName: null,
        isPlayer: true,
        badgeLabel: 'Single Parent',
      })
    }

    // Player with each spouse
    playerSpouses.forEach((sp) => {
      allAdultCandidates.push({
        key: `${PLAYER_FORM_ID}_${sp.formId}`,
        formId: PLAYER_FORM_ID,
        name: `${playerDisplayName} & ${sp.name}`,
        race: sp.race,
        pluginName: sp.pluginName,
        partnerId: sp.formId,
        partnerName: sp.name,
        spouseHome: sp.currentHome,
        isPlayer: true,
        // badgeLabel: sp.currentHome ? `Assigned Home: ${sp.currentHome}` : 'No Assigned Home',
      })
    })
  }

  // 2. Add other adult candidates
  otherAdultMap.forEach((adult) => {
    if (playerSpouses.find(ps => ps.formId == adult.formId)) {
      return;
    }
    let partnerId: string | null = null
    for (const r of relationships) {
      if (r.status === RelationshipStatus.Married) {
        if (r.actorA === adult.formId) {
          partnerId = r.actorB
          break
        } else if (r.actorB === adult.formId) {
          partnerId = r.actorA
          break
        }
      }
    }
    const partnerName = partnerId ? getActorName(partnerId) : null
    allAdultCandidates.push({
      key: adult.formId,
      formId: adult.formId,
      name: adult.name,
      race: adult.race,
      pluginName: adult.pluginName,
      partnerId,
      partnerName,
      isPlayer: false,
      badgeLabel: partnerName ? `Spouse: ${partnerName}` : 'Single',
    })
  })

  // Helper to determine if an adult candidate is a current legal guardian of the currently selected child
  const isGuardianCandidate = (adult: AdoptiveCandidateItem) => {
    if (!selectedChildInfo) return 0
    const childRec = selectedChildInfo.childRecord
    if (!childRec) return 0

    const gA = childRec.guardianAId
    const gB = childRec.guardianBId

    // For a couple candidate (adult + partner), BOTH must match the child's guardians
    if (adult.partnerId) {
      if (gA && gB && ((gA === adult.formId && gB === adult.partnerId) || (gB === adult.formId && gA === adult.partnerId))) {
        return 2
      }
      return 0
    }

    // For a single candidate, they must match guardianA or guardianB
    if (gA && adult.formId === gA) {
      return !gB ? 2 : 1
    }
    if (gB && adult.formId === gB) {
      return !gA ? 2 : 1
    }

    return 0
  }

  // Helper to determine if an adult candidate is a biological parent of the currently selected child
  const getBioParentInfo = (adult: AdoptiveCandidateItem): { isBio: boolean; score: number; label: string } | null => {
    if (!selectedChildInfo) return null
    const childRec = selectedChildInfo.childRecord
    if (!childRec) return null

    const bioM = childRec.bioMotherId
    const bioF = childRec.bioFatherId

    if (!bioM && !bioF) return null

    const adultMatchesM = Boolean(bioM && adult.formId === bioM)
    const adultMatchesF = Boolean(bioF && adult.formId === bioF)
    const partnerMatchesM = Boolean(bioM && adult.partnerId && adult.partnerId === bioM)
    const partnerMatchesF = Boolean(bioF && adult.partnerId && adult.partnerId === bioF)

    // Couple matches both biological parents
    if ((adultMatchesM && partnerMatchesF) || (adultMatchesF && partnerMatchesM)) {
      return {
        isBio: true,
        score: 2,
        label: 'Biological Parents',
      }
    }

    // Adult is biological mother
    if (adultMatchesM) {
      return {
        isBio: true,
        score: 1,
        label: 'Biological Mother',
      }
    }

    // Adult is biological father
    if (adultMatchesF) {
      return {
        isBio: true,
        score: 1,
        label: 'Biological Father',
      }
    }

    // Partner is biological mother
    if (partnerMatchesM) {
      return {
        isBio: true,
        score: 1,
        label: adult.partnerName ? `Spouse (${adult.partnerName}) is Bio Mother` : 'Spouse is Bio Mother',
      }
    }

    // Partner is biological father
    if (partnerMatchesF) {
      return {
        isBio: true,
        score: 1,
        label: adult.partnerName ? `Spouse (${adult.partnerName}) is Bio Father` : 'Spouse is Bio Father',
      }
    }

    return null
  }

  // Priority score for sorting candidates (bio parents and guardians to the top)
  const getCandidatePriority = (adult: AdoptiveCandidateItem) => {
    const guardianScore = isGuardianCandidate(adult)
    const bioInfo = getBioParentInfo(adult)
    const bioScore = bioInfo ? bioInfo.score : 0

    // Highest priority: candidate who is both guardian and biological parent
    if (guardianScore > 0 && bioScore > 0) {
      return 100 + guardianScore * 10 + bioScore
    }
    // Biological parents sorted to the top
    if (bioScore > 0) {
      return 80 + bioScore * 10
    }
    // Current guardians
    if (guardianScore > 0) {
      return 60 + guardianScore * 10
    }
    return 0
  }

  // Filter and sort adult candidates (bringing selected child's bio parents and guardians to the top)
  const filteredAdults = useMemo(() => {
    const list = allAdultCandidates.filter((adult) => {
      if (!searchParentQuery.trim()) return true
      const q = searchParentQuery.toLowerCase()
      const nameMatch = adult.name.toLowerCase().includes(q)
      const formIdMatch = adult.formId.toLowerCase().includes(q)
      const partnerMatch = (adult.partnerName || '').toLowerCase().includes(q)
      const bioInfo = getBioParentInfo(adult)
      const bioMatch = Boolean(bioInfo && (bioInfo.label.toLowerCase().includes(q) || 'biological'.includes(q) || 'bio parent'.includes(q)))
      return nameMatch || formIdMatch || partnerMatch || bioMatch
    })

    return [...list].sort((a, b) => {
      const aPriority = getCandidatePriority(a)
      const bPriority = getCandidatePriority(b)
      if (aPriority !== bPriority) {
        return bPriority - aPriority
      }
      return 0
    })
  }, [allAdultCandidates, searchParentQuery, selectedChildInfo])

  const availableUnassignedCount = childrenWithAdoptionInfo.filter((item) => item.isUnassigned && !item.isBlocked).length
  const adoptedChildrenCount = childrenWithAdoptionInfo.filter((item) => item.isAdopted).length
  const biologicalChildrenCount = childrenWithAdoptionInfo.filter((item) => item.isBiological).length

  // Adopt Child Handler
  const handleAdopt = (candidate: AdoptiveCandidateItem) => {
    if (!selectedChildInfo || selectedChildInfo.isBlocked) return

    const childId = selectedChildInfo.profile.formId
    const childName = selectedChildInfo.profile.name

    if (candidate.isPlayer) {
      // Rule 1: If player doesn't have house - don't allow to adopt
      if (playerHomes.length === 0) return

      // Rule 2: If player single and doesn't have spouse co-adopter
      if (!candidate.partnerId) {
        if (playerHomes.length === 1) {
          // single house - choose it as residence to newly adopted child immediately
          addChild({
            childId,
            guardianAId: PLAYER_FORM_ID,
            houseId: playerHomes[0].formId,
            assignSpouseHome: false,
          })
        } else {
          // multiple houses - show list of player houses
          setHouseModalState({
            isOpen: true,
            childId,
            childName,
            actorA: PLAYER_FORM_ID,
            actorB: null,
            spouseName: null,
            assignSpouseHome: false,
          })
        }
        return
      }

      // Rule 3: If player adopt with their spouse
      const spouseProfile = npcProfiles.get(candidate.partnerId)
      const spouseHomeStr = spouseProfile?.currentHome || candidate.spouseHome
      const assignedHouse = spouseHomeStr
        ? playerHomes.find(
          (h) =>
            h.name === spouseHomeStr ||
            h.name.toLowerCase() === spouseHomeStr.toLowerCase() ||
            h.formId === spouseHomeStr
        )
        : null

      if (assignedHouse) {
        // Spouse already assigned to house, assign newly adopted child there
        addChild({
          childId,
          guardianAId: PLAYER_FORM_ID,
          guardianBId: candidate.partnerId,
          houseId: assignedHouse.formId,
          assignSpouseHome: false,
        })
      } else {
        // Spouse doesn't have assigned house
        if (playerHomes.length === 1) {
          // single house - assign spouse there (with disable maras spouse sandbox package) and assign child
          addChild({
            childId,
            guardianAId: PLAYER_FORM_ID,
            guardianBId: candidate.partnerId,
            houseId: playerHomes[0].formId,
            assignSpouseHome: true,
          })
        } else {
          // multiple houses - show house selections list
          setHouseModalState({
            isOpen: true,
            childId,
            childName,
            actorA: PLAYER_FORM_ID,
            actorB: candidate.partnerId,
            spouseName: candidate.partnerName || getActorName(candidate.partnerId),
            assignSpouseHome: true,
          })
        }
      }
      return
    }

    // Non-player NPC adoption fallback
    addChild({
      childId,
      guardianAId: candidate.formId,
      guardianBId: candidate.partnerId || undefined,
    })
  }

  return (
    <div className="flex h-full w-full gap-[1rem] overflow-hidden select-none font-ui">
      {/* LEFT COLUMN: Child-Centric List of All Skyrim Children */}
      <div className="w-[22rem] flex flex-col gap-[0.75rem] bg-panel border border-border-divider rounded-lg p-[1rem] overflow-hidden flex-shrink-0">
        {/* Header */}
        <div className="flex items-center gap-[0.5rem] pb-[0.5rem] border-b border-border-divider">
          <Baby className="w-[1.25em] h-[1.25em] text-accent" />
          <div>
            <h2 className="text-sm font-bold text-accent-bright">Skyrim Children</h2>
            <p className="text-[0.6875rem] text-foreground-muted">Select child to assign adoption</p>
          </div>
        </div>

        {/* Filter Controls */}
        <div className="space-y-[0.5rem]">
          <SearchInput
            value={searchChildQuery}
            placeholder="Search by name, race, or location..."
            onChange={setSearchChildQuery}
          />

          <div className="grid grid-cols-2 gap-[0.25rem] text-xs">
            <button
              onClick={() => setFilterStatus('all')}
              className={`py-[0.375rem] px-[0.5rem] rounded border font-semibold transition-colors cursor-pointer text-[0.6875rem] flex items-center justify-between ${filterStatus === 'all'
                ? 'bg-accent text-panel border-accent'
                : 'bg-panel-inset hover:bg-interactive-hover text-foreground-secondary border-border-subtle'
                }`}
            >
              <span>All</span>
              <span className="opacity-80">({childrenWithAdoptionInfo.length})</span>
            </button>
            <button
              onClick={() => setFilterStatus('available')}
              className={`py-[0.375rem] px-[0.5rem] rounded border font-semibold transition-colors cursor-pointer text-[0.6875rem] flex items-center justify-between ${filterStatus === 'available'
                ? 'bg-status-success text-panel border-status-success'
                : 'bg-panel-inset hover:bg-interactive-hover text-foreground-secondary border-border-subtle'
                }`}
            >
              <span>Available</span>
              <span className="opacity-80">({availableUnassignedCount})</span>
            </button>
            <button
              onClick={() => setFilterStatus('adopted')}
              className={`py-[0.375rem] px-[0.5rem] rounded border font-semibold transition-colors cursor-pointer text-[0.6875rem] flex items-center justify-between ${filterStatus === 'adopted'
                ? 'bg-accent-soft text-accent-bright border-border-accent'
                : 'bg-panel-inset hover:bg-interactive-hover text-foreground-secondary border-border-subtle'
                }`}
            >
              <span>Adopted</span>
              <span className="opacity-80">({adoptedChildrenCount})</span>
            </button>
            <button
              onClick={() => setFilterStatus('biological')}
              className={`py-[0.375rem] px-[0.5rem] rounded border font-semibold transition-colors cursor-pointer text-[0.6875rem] flex items-center justify-between ${filterStatus === 'biological'
                ? 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40'
                : 'bg-panel-inset hover:bg-interactive-hover text-foreground-secondary border-border-subtle'
                }`}
            >
              <span>Biological</span>
              <span className="opacity-80">({biologicalChildrenCount})</span>
            </button>
          </div>
        </div>

        {/* Children Scroll List */}
        <ScrollArea className="flex-1 min-h-0 border border-border-subtle rounded-md p-[0.375rem] bg-panel-inset">
          <div className="space-y-[0.375rem]">
            {filteredChildren.map((item) => {
              const child = item.profile
              const isSelected = selectedChildId === child.formId
              const isFemale = resolveIsFemale(item.childRecord, child)
              const { sonOrDaughter } = getChildGenderNouns(isFemale)

              return (
                <div
                  key={child.formId}
                  onClick={() => setSelectedChildId(child.formId)}
                  className={`p-[0.625rem] rounded-md border flex items-center justify-between text-xs cursor-pointer transition-all ${isSelected
                    ? 'bg-accent-soft text-accent-bright border-border-accent font-semibold shadow-sm'
                    : 'bg-panel-raised hover:bg-interactive-hover text-foreground-primary border-border-divider'
                    }`}
                >
                  <div className="flex items-center gap-[0.5rem]">
                    <Baby className={`w-[1.125em] h-[1.125em] flex-shrink-0 ${isSelected ? 'text-accent' : 'text-foreground-muted'}`} />
                    <div>
                      <div className="text-xs font-bold flex items-center gap-[0.375rem]">
                        <span>{child.name}</span>
                        <span className="text-[0.6875rem] font-mono text-foreground-muted">({child.formId})</span>
                        {/* {isFemale !== undefined && (
                          <span className={`text-[0.625rem] font-bold px-[0.25rem] py-[0.0625rem] rounded border ${isFemale ? 'bg-pink-500/20 text-pink-300 border-pink-500/30' : 'bg-blue-500/20 text-blue-300 border-blue-500/30'}`} title={isFemale ? 'Daughter' : 'Son'}>
                            {isFemale ? '♀ Daughter' : '♂ Son'}
                          </span>
                        )} */}
                      </div>
                      <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem] flex-wrap">
                        <span>{child.currentHome || child.race}</span>
                        {child.pluginName && (
                          <>
                            <span>•</span>
                            <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${child.pluginName}`}>
                              {child.pluginName}
                            </span>
                          </>
                        )}
                        {item.isAdopted && item.childRecord?.timestamp != null && item.childRecord.timestamp > 0 && (
                          <span className="ml-[0.25rem]">
                            • {formatGameDay(item.childRecord.timestamp, currentGameDay)}
                          </span>
                        )}
                        {item.isBiological && (item.parentAName || item.parentBName) && (
                          <span className="ml-[0.25rem] text-emerald-400/90 truncate max-w-[10rem]">
                            • {item.parentAName && item.parentBName
                              ? `Parents: ${item.parentAName} and ${item.parentBName}`
                              : `Parent: ${item.parentAName || item.parentBName}`}
                          </span>
                        )}
                        {item.isBlocked && item.blockedReason && (
                          <span className="ml-[0.25rem] text-amber-400/90 truncate max-w-[14rem]" title={item.blockedReason}>
                            • {item.blockedReason}
                          </span>
                        )}
                      </div>
                    </div>
                  </div>

                  <div className="flex items-center gap-[0.25rem] shrink-0 whitespace-nowrap">
                    {item.isAdopted ? (
                      <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-semibold bg-accent-soft text-accent-bright border border-border-accent whitespace-nowrap">
                        Adopted {sonOrDaughter} {isFemale ? '♀' : '♂'}
                      </span>
                    ) : item.isBiological ? (
                      <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-semibold bg-emerald-500/20 text-emerald-300 border border-emerald-500/30 whitespace-nowrap">
                        Biological {sonOrDaughter} {isFemale ? '♀' : '♂'}
                      </span>
                    ) : item.isBlocked ? (
                      <span
                        className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-semibold bg-amber-500/20 text-amber-300 border border-amber-500/30 whitespace-nowrap"
                        title={item.blockedReason || 'Blocked from adoption'}
                      >
                        Blocked {isFemale ? '♀' : '♂'}
                      </span>
                    ) : (
                      <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-semibold bg-status-success-soft text-status-success border border-status-success whitespace-nowrap">
                        Available {isFemale ? '♀' : '♂'}
                      </span>
                    )}
                  </div>
                </div>
              )
            })}

            {filteredChildren.length === 0 && (
              <div className="p-[1rem] text-center text-xs text-foreground-muted">
                No children match search filter.
              </div>
            )}
          </div>
        </ScrollArea>
      </div>

      {/* RIGHT COLUMN: Target Adoptive Parent Selection for Selected Child */}
      <div className="flex-1 flex flex-col gap-[0.75rem] bg-panel border border-border-divider rounded-lg p-[1rem] overflow-hidden">
        {selectedChildInfo ? (
          <>
            {/* Header / Selected Child Banner */}
            <div className="p-[0.875rem] rounded-lg bg-panel-raised border border-border-accent flex items-center justify-between gap-[1rem] shadow-sm">
              <div className="flex items-center gap-[0.75rem]">
                <div className="p-[0.625rem] rounded-full bg-accent-soft text-accent border border-border-accent">
                  <Baby className="w-[1.75em] h-[1.75em]" />
                </div>
                <div>
                  <div className="flex items-center gap-[0.5rem] flex-wrap">
                    <h2 className="text-base font-bold text-accent-bright">{selectedChildInfo.profile.name}</h2>
                    <button
                      onClick={() => openNpcDetail(selectedChildInfo.profile.formId)}
                      className="text-foreground-muted hover:text-accent p-[0.125rem] rounded cursor-pointer transition-colors"
                      title="Inspect Child Profile"
                    >
                      <ExternalLink className="w-[0.875em] h-[0.875em]" />
                    </button>

                    {(() => {
                      const isF = resolveIsFemale(selectedChildInfo.childRecord, selectedChildInfo.profile)
                      if (isF === undefined) return null
                      return (
                        <span className={`text-[0.625rem] font-bold px-[0.25rem] py-[0.0625rem] rounded border ${isF ? 'bg-pink-500/20 text-pink-300 border-pink-500/30' : 'bg-blue-500/20 text-blue-300 border-blue-500/30'}`} title={isF ? 'Daughter' : 'Son'}>
                          {isF ? '♀ Daughter' : '♂ Son'}
                        </span>
                      )
                    })()}


                  </div>
                  <div className="flex items-center gap-[0.5rem] flex-wrap">
                    <span className="text-xs text-foreground-muted font-mono">({selectedChildInfo.profile.formId})</span>
                    {selectedChildInfo.profile.pluginName && (
                      <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${selectedChildInfo.profile.pluginName}`}>
                        {selectedChildInfo.profile.pluginName}
                      </span>
                    )}
                  </div>

                  <div className="text-xs text-foreground-secondary flex items-center gap-[0.625rem] mt-[0.125rem] flex-wrap">
                    <span>Race: <strong className="text-foreground-primary">{selectedChildInfo.profile.race}</strong></span>
                    <span>•</span>
                    <span>Residence: <strong className="text-foreground-primary">{selectedChildInfo.profile.currentHome || 'Skyrim'}</strong></span>
                    {selectedChildInfo.isAdopted && selectedChildInfo.bioParentName && (
                      <>
                        <span>•</span>
                        <span>Original Bio Parent: <strong className="text-foreground-primary">{selectedChildInfo.bioParentName}</strong></span>
                      </>
                    )}
                  </div>
                </div>
              </div>

              {/* Status Pill */}
              <div className="shrink-0 whitespace-nowrap">
                {selectedChildInfo.isAdopted || selectedChildInfo.isBiological ? (
                  <span className="px-[0.75rem] py-[0.375rem] rounded text-xs font-semibold bg-accent-soft text-accent-bright border border-border-accent flex items-center gap-[0.375rem]">
                    <Heart className="w-[1em] h-[1em] text-accent" />
                    <span>Has Family</span>
                  </span>
                ) : selectedChildInfo.isBlocked ? (
                  <span className="px-[0.75rem] py-[0.375rem] rounded text-xs font-semibold bg-amber-500/20 text-amber-300 border border-amber-500/30 flex items-center gap-[0.375rem]">
                    <AlertCircle className="w-[1em] h-[1em] text-amber-400" />
                    <span>Adoption Blocked</span>
                  </span>
                ) : (
                  <span className="px-[0.75rem] py-[0.375rem] rounded text-xs font-semibold bg-status-success-soft text-status-success border border-status-success flex items-center gap-[0.375rem]">
                    <ShieldCheck className="w-[1em] h-[1em]" />
                    <span>Eligible for Adoption</span>
                  </span>
                )}
              </div>
            </div>

            {/* Sub-Header & Search Parent Filter */}
            <div className="flex items-center justify-between pt-[0.25rem]">
              <div className="flex items-center gap-[0.375rem]">
                <User className="w-[1.125em] h-[1.125em] text-accent" />
                <h3 className="text-xs font-bold text-foreground-primary uppercase tracking-wider">
                  Select Adoptive Parent NPC:
                </h3>
              </div>

              <div className="w-[14rem]">
                <SearchInput
                  value={searchParentQuery}
                  placeholder="Filter adult NPCs/parents/plugins..."
                  onChange={setSearchParentQuery}
                />
              </div>
            </div>

            {/* Blocked Child Warning Banner */}
            {selectedChildInfo.isBlocked && (
              <div className="p-[0.75rem] rounded-lg bg-amber-500/10 border border-amber-500/30 text-amber-300 text-xs flex items-center gap-[0.625rem] shadow-sm">
                <AlertCircle className="w-[1.25rem] h-[1.25rem] flex-shrink-0 text-amber-400" />
                <div>
                  <span className="font-semibold block">Cannot Adopt {selectedChildInfo.profile.name}</span>
                  <span className="text-[0.6875rem] text-foreground-muted">
                    {selectedChildInfo.blockedReason || 'This child is currently unavailable for adoption.'}
                  </span>
                </div>
              </div>
            )}

            {/* Zero-Houses Warning Banner */}
            {playerHomes.length === 0 && (
              <div className="p-[0.75rem] rounded-lg bg-amber-500/10 border border-amber-500/30 text-amber-300 text-xs flex items-center gap-[0.625rem] shadow-sm">
                <AlertCircle className="w-[1.25rem] h-[1.25rem] flex-shrink-0 text-amber-400" />
                <div>
                  <span className="font-semibold block">No Homes Owned</span>
                  <span className="text-[0.6875rem] text-foreground-muted">
                    The Dragonborn must purchase or acquire at least one property before adopting children into the household.
                  </span>
                </div>
              </div>
            )}

            {/* List of Adult Candidate NPCs for Adoption */}
            <ScrollArea key={selectedChildId} className="flex-1 min-h-0 pr-[0.375rem]">
              <div className="space-y-[0.625rem]">
                {filteredAdults.map((adult) => {
                  const guardianScore = isGuardianCandidate(adult)
                  const isCurrentGuardian = guardianScore > 0
                  const bioInfo = getBioParentInfo(adult)
                  const isBioParent = Boolean(bioInfo)

                  return (
                    <div
                      key={adult.key}
                      className={`p-[0.875rem] rounded-lg border flex items-center justify-between gap-[1rem] transition-all ${
                        isCurrentGuardian
                          ? isBioParent
                            ? 'bg-emerald-500/10 border-emerald-500/30'
                            : 'bg-accent-soft/40 border-border-accent'
                          : isBioParent
                          ? 'bg-emerald-500/10 border-emerald-500/30 hover:bg-emerald-500/15'
                          : 'bg-panel-inset hover:bg-panel-raised border-border-divider'
                      }`}
                    >
                      {/* Adult NPC Info */}
                      <div className="flex items-center gap-[0.75rem]">
                        <div
                          className={`p-[0.5rem] rounded-full border ${
                            isBioParent
                              ? 'bg-emerald-500/20 text-emerald-400 border-emerald-500/30'
                              : isCurrentGuardian
                              ? 'bg-accent-soft text-accent border-border-accent'
                              : 'bg-panel-raised text-accent border border-border-subtle'
                          }`}
                        >
                          {isBioParent ? (
                            <Heart className="w-[1.25em] h-[1.25em] fill-emerald-400/30 text-emerald-400" />
                          ) : (
                            <User className="w-[1.25em] h-[1.25em]" />
                          )}
                        </div>

                        <div>
                          <div className="text-sm font-bold text-foreground-primary flex items-center gap-[0.375rem] flex-wrap">
                            <span>{adult.name}</span>
                            {adult.badgeLabel && (
                              <span className="text-xs text-foreground-muted font-normal">
                                ({adult.badgeLabel})
                              </span>
                            )}
                            {bioInfo && (
                              <span className="px-[0.375rem] py-[0.0625rem] rounded text-[0.6875rem] font-semibold bg-emerald-500/20 text-emerald-300 border border-emerald-500/30 flex items-center gap-[0.25rem] whitespace-nowrap shadow-sm">
                                <Heart className="w-[0.75rem] h-[0.75rem] fill-emerald-400 text-emerald-400 shrink-0" />
                                <span>{bioInfo.label}</span>
                              </span>
                            )}
                          </div>

                          <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.5rem] mt-[0.125rem] flex-wrap">
                            <span className="font-mono">{adult.formId}</span>
                            <span>•</span>
                            <span>Race: <strong className="text-foreground-secondary">{adult.race}</strong></span>
                            {adult.pluginName && (
                              <>
                                <span>•</span>
                                <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${adult.pluginName}`}>
                                  {adult.pluginName}
                                </span>
                              </>
                            )}
                          </div>
                        </div>
                      </div>

                      {/* Adoption Trigger Action */}
                      <div className="shrink-0 whitespace-nowrap">
                        {isCurrentGuardian ? (
                          <span
                            className={`px-[0.75rem] py-[0.375rem] rounded text-xs font-semibold flex items-center gap-[0.25rem] whitespace-nowrap ${
                              isBioParent
                                ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30'
                                : 'bg-accent-soft text-accent-bright border border-border-accent'
                            }`}
                          >
                            <ShieldCheck className="w-[1em] h-[1em]" />
                            <span className="whitespace-nowrap">
                              {isBioParent ? 'Biological Parent & Guardian' : 'Current Guardian'}
                            </span>
                          </span>
                        ) : selectedChildInfo.isBlocked ? (
                          <AppTooltip content={selectedChildInfo.blockedReason || 'Cannot adopt this child'}>
                            <div>
                              <button
                                disabled
                                className="px-[0.875rem] py-[0.375rem] bg-panel-inset text-foreground-muted font-semibold text-xs rounded border border-border-divider flex items-center gap-[0.375rem] cursor-not-allowed opacity-60 whitespace-nowrap"
                              >
                                <AlertCircle className="w-[1em] h-[1em] text-amber-400" />
                                <span className="whitespace-nowrap">Adoption Blocked</span>
                              </button>
                            </div>
                          </AppTooltip>
                        ) : adult.isPlayer && playerHomes.length === 0 ? (
                          <AppTooltip content="Requires an owned home to adopt">
                            <div>
                              <button
                                disabled
                                className="px-[0.875rem] py-[0.375rem] bg-panel-inset text-foreground-muted font-semibold text-xs rounded border border-border-divider flex items-center gap-[0.375rem] cursor-not-allowed opacity-60 whitespace-nowrap"
                              >
                                <Home className="w-[1em] h-[1em]" />
                                <span className="whitespace-nowrap">No Home Owned</span>
                              </button>
                            </div>
                          </AppTooltip>
                        ) : (
                          <button
                            onClick={() => handleAdopt(adult)}
                            className={`px-[0.875rem] py-[0.375rem] font-semibold text-xs rounded transition-colors cursor-pointer flex items-center gap-[0.375rem] shadow-sm shrink-0 whitespace-nowrap ${
                              isBioParent
                                ? 'bg-emerald-600 hover:bg-emerald-500 text-white'
                                : 'bg-accent hover:bg-accent-bright text-panel'
                            }`}
                          >
                            <PlusCircle className="w-[1em] h-[1em] shrink-0" />
                            <span className="whitespace-nowrap">Adopt {selectedChildInfo.profile.name}</span>
                          </button>
                        )}
                      </div>
                    </div>
                  )
                })}

                {filteredAdults.length === 0 && (
                  <div className="p-[2rem] text-center text-xs text-foreground-muted bg-panel-inset border border-border-subtle rounded-lg space-y-[0.25rem]">
                    <Users className="w-[2em] h-[2em] text-accent/50 mx-auto" />
                    <div className="font-semibold text-foreground-primary">No Adult NPCs Found</div>
                    <div>Try adjusting your search query.</div>
                  </div>
                )}
              </div>
            </ScrollArea>
          </>
        ) : (
          <div className="flex-1 flex flex-col items-center justify-center text-center p-[2rem] text-foreground-muted gap-[0.5rem]">
            <Baby className="w-[2.5em] h-[2.5em] text-accent/50" />
            <div className="text-sm font-semibold text-foreground-primary">No Child Selected</div>
            <div className="text-xs">Select a child from the left column to assign adoption.</div>
          </div>
        )}
      </div>

      {/* Dedicated Residence Selection Modal */}
      {houseModalState && (
        <HouseSelectionModal
          isOpen={houseModalState.isOpen}
          onClose={() => setHouseModalState(null)}
          onSelectHouse={(house) => {
            addChild({
              childId: houseModalState.childId,
              guardianAId: houseModalState.actorA,
              guardianBId: houseModalState.actorB || undefined,
              houseId: house.formId,
              assignSpouseHome: houseModalState.assignSpouseHome,
            })
            setHouseModalState(null)
          }}
          childName={houseModalState.childName}
          spouseName={houseModalState.spouseName}
          houses={housesWithOccupants}
        />
      )}
    </div>
  )
}
