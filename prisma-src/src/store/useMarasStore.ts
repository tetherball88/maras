import { create } from 'zustand'
import {
  NPCProfile,
  RelationshipPair,
  AffectionData,
  MarasSettings,
  RelationshipStatus,
  PlayerHouse,
  SocialClass,
  SkillType,
  Temperament,
  CompactNPC,
  DataDumpResult,
  AiMatchSuggestion,
  MatchmakingSettings,
  MatchmakingOrientation,
  NpcMemoryEntry,
  HouseholdBuffsData,
  ChildRecord,
  CheatingRecord,
} from '../types'
import {
  mockNpcProfiles,
  mockRelationships,
  mockChildren,
  mockCheatingRecords,
  mockAffectionData,
  mockHierarchyRanks,
  mockSettings,
  mockPlayerHomes,
  mockAllSkyrimNpcs,
  mockHouseholdBuffs,
  PLAYER_FORM_ID,
} from '../data/mockData'
import { sendMarasAction } from './prismaBridge'
import { isInGame as checkIsInGame } from '../lib/utils'
import { areSameFormId } from '../utils/statusAvailability'

export type { AiMatchSuggestion }

interface MarasState {
  isOpen: boolean
  isInGame: boolean
  statusMsg: string
  activeTab: string
  activeSubSection: string | null

  // NPC Detail Screen State
  npcDetailId: string | null
  npcDetailPreviousContext: { tab: string; subSection: string | null } | null
  npcDetailPartnerId: string | null

  // Matchmaking Modal State
  matchmakingModalOpen: boolean
  matchmakingTargetNpcId: string | null
  matchmakingScanType: 'target' | 'global'
  aiSuggestions: AiMatchSuggestion[]
  isGeneratingAiSuggestions: boolean

  // Legacy Selection & UI states (retained for compatibility)
  selectedNpcId: string | null
  selectedNpcRelationsId: string | null
  matchmakingNpcA: string | null
  matchmakingNpcB: string | null

  // Search state (Matchmaking Tab & Modal round trip to SKSE)
  searchQueryA: string
  searchQueryB: string
  searchQueryModal: string
  searchResultsA: NPCProfile[]
  searchResultsB: NPCProfile[]
  searchResultsModal: NPCProfile[]
  isSearchingA: boolean
  isSearchingB: boolean
  isSearchingModal: boolean

  // All Skyrim NPCs Client Index
  allSkyrimNpcs: CompactNPC[]
  isLoadingAllNpcs: boolean
  setAllSkyrimNpcs: (npcs: CompactNPC[]) => void
  fetchAllSkyrimNpcs: () => void

  // Core Data
  currentGameDay: number
  npcProfiles: Map<string, NPCProfile>
  relationships: RelationshipPair[]
  children: Map<string, ChildRecord>
  cheatingRecords: CheatingRecord[]
  affectionData: Map<string, AffectionData>
  hierarchyRanks: [string, string, string]
  playerHomes: PlayerHouse[]
  settings: MarasSettings
  matchmakingSettings: MatchmakingSettings
  householdBuffs: HouseholdBuffsData | null
  isLoadingBuffs: boolean
  setHouseholdBuffs: (buffs: HouseholdBuffsData | null) => void
  fetchHouseholdBuffs: () => void

  // NPC Detail Actions
  openNpcDetail: (npcId: string) => void
  closeNpcDetail: () => void
  setNpcDetailPartnerId: (partnerId: string | null) => void

  // Matchmaking Modal & AI Suggestions Actions
  setMatchmakingModalOpen: (open: boolean, targetNpcId?: string | null) => void
  setMatchmakingScanType: (type: 'target' | 'global') => void
  setAiSuggestions: (suggestions: AiMatchSuggestion[]) => void
  generateAiSuggestions: (npcId: string) => void
  generateGlobalAiSuggestions: (autoApply?: boolean) => void
  generateMatchMemories: (targetFormId: string, partnerFormId: string, targetStatus: string, action?: string) => void
  receiveGeneratedMemories: (data: { partnerFormId: string; memories: NpcMemoryEntry[] }) => void
  toggleAiSuggestionSelected: (formId: string) => void
  updateAiSuggestionStatus: (formId: string, status: RelationshipStatus) => void
  applyAiSuggestions: (npcId: string) => void
  setMatchmakingSettings: (settings: Partial<MatchmakingSettings>) => void
  updateMatchmakingSettings: (settings: Partial<MatchmakingSettings>) => void

  // Teleport & Child Companion Actions
  teleportPlayerToNpc: (npcId: string) => void
  teleportNpcToPlayer: (npcId: string) => void
  fleeChildrenFromCell: () => void
  dismissChildCritter: (npcId: string) => void
  dismissChildPet: (npcId: string) => void
  spawnChild: (childId: string) => void
  handleChildPromoted: (oldId: string, newId: string) => void
  evaluatingChildId: string | null
  evaluateChildSecret: (childId: string) => void

  // General Actions
  setStatusMsg: (msg: string) => void
  setIsOpen: (isOpen: boolean) => void
  setActiveTab: (tab: string) => void
  setActiveSubSection: (subSection: string | null) => void
  setSelectedNpcId: (id: string | null) => void
  setSelectedNpcRelationsId: (id: string | null) => void
  setMatchmakingNpcA: (id: string | null) => void
  setMatchmakingNpcB: (id: string | null) => void
  detailPanelOpen: boolean
  setDetailPanelOpen: (open: boolean) => void
  setPlayerHomes: (homes: PlayerHouse[]) => void
  setSearchQuery: (side: 'a' | 'b' | 'modal', query: string) => void
  setSearchResults: (side: 'a' | 'b' | 'modal', results: any[]) => void

  // Data Mutators
  setLiveGameData: (data: {
    currentGameDay?: number
    npcProfiles?: NPCProfile[]
    relationships?: RelationshipPair[]
    children?: ChildRecord[]
    cheatingRecords?: CheatingRecord[]
    affectionData?: AffectionData[]
    hierarchyRanks?: [string, string, string]
    playerHomes?: PlayerHouse[]
    settings?: Partial<MarasSettings>
  }) => void
  updateAffection: (formId: string, value: number, skipBridge?: boolean) => void
  updateNPCProfile: (formId: string, updates: Partial<NPCProfile>, skipBridge?: boolean) => void
  setPairIsOpen: (actorA: string, actorB: string, isOpen: boolean, fromMatchmaking?: boolean) => void
  setHierarchyRank: (formId: string, rankIndex: number) => void
  assignRelationshipStatus: (actorA: string, actorB: string, status: RelationshipStatus, dualMemories?: NpcMemoryEntry[], fromMatchmaking?: boolean) => void
  handleRealtimeStatusChanged: (actorA: string, actorB: string, status: RelationshipStatus, isOpen?: boolean) => void
  removeRelationship: (actorA: string, actorB: string, fromMatchmaking?: boolean) => void
  addChild: (
    child: {
      childId: string
      guardianAId: string
      guardianBId?: string
      houseId?: string
      assignSpouseHome?: boolean
    },
    skipBridge?: boolean
  ) => void
  removeChild: (childId: string, skipBridge?: boolean) => void
  updateChild: (childId: string, updates: Partial<ChildRecord>, skipBridge?: boolean) => void
  relocateFamilyUnit: (params: { childId: string; home: string; coParentId?: string; siblingIds?: string[] }) => void
  setPaternitySecretEdges: (childId: string, isSecretForBioFather: boolean, isSecretForPartner: boolean) => void
  updateSetting: <K extends keyof MarasSettings>(key: K, value: MarasSettings[K]) => void

  // Diagnostics & Data Export
  isDumpingData: boolean
  lastDumpResult: DataDumpResult | null
  dumpData: () => void
  setIsDumpingData: (isDumping: boolean) => void
  setLastDumpResult: (result: DataDumpResult | null) => void

  isDumpingNPCIndex: boolean
  lastNPCIndexDumpResult: DataDumpResult | null
  dumpNPCIndex: () => void
  setIsDumpingNPCIndex: (isDumping: boolean) => void
  setLastNPCIndexDumpResult: (result: DataDumpResult | null) => void

  closeUi: () => void
  logTestSignal: () => void
}

const isGameBuild = checkIsInGame()

const initialNpcMap = new Map<string, NPCProfile>()
if (!isGameBuild) {
  mockNpcProfiles.forEach((npc) => initialNpcMap.set(npc.formId, npc))
}

const initialAffectionMap = new Map<string, AffectionData>()
if (!isGameBuild) {
  mockAffectionData.forEach((aff) => initialAffectionMap.set(aff.formId, aff))
}

const initialChildMap = new Map<string, ChildRecord>()
if (!isGameBuild) {
  mockChildren.forEach((c) => initialChildMap.set(c.childId, c))
}

// Timers and pending state for debounced bridge updates
const setAffectionDebounceTimers = new Map<string, ReturnType<typeof setTimeout>>()
const pendingAffectionUpdates = new Map<string, number>()

let matchmakingSettingsDebounceTimer: ReturnType<typeof setTimeout> | null = null
let pendingMatchmakingSettingsUpdates: Partial<MatchmakingSettings> = {}

const updateSettingDebounceTimers = new Map<string, ReturnType<typeof setTimeout>>()
const pendingSettingUpdates = new Map<string, any>()

export function flushAllPendingDebouncedActions() {
  // 1. Flush pending Matchmaking Settings
  if (matchmakingSettingsDebounceTimer) {
    clearTimeout(matchmakingSettingsDebounceTimer)
    matchmakingSettingsDebounceTimer = null
  }
  if (Object.keys(pendingMatchmakingSettingsUpdates).length > 0) {
    const updates = { ...pendingMatchmakingSettingsUpdates }
    pendingMatchmakingSettingsUpdates = {}
    sendMarasAction('updateMatchmakingSettings', updates)
  }

  // 2. Flush pending General Settings
  for (const [, timer] of updateSettingDebounceTimers.entries()) {
    clearTimeout(timer)
  }
  updateSettingDebounceTimers.clear()
  for (const [key, value] of pendingSettingUpdates.entries()) {
    sendMarasAction('updateSetting', { key, value })
  }
  pendingSettingUpdates.clear()

  // 3. Flush pending Affection changes
  for (const [, timer] of setAffectionDebounceTimers.entries()) {
    clearTimeout(timer)
  }
  setAffectionDebounceTimers.clear()
  for (const [formId, value] of pendingAffectionUpdates.entries()) {
    sendMarasAction('setAffection', { formId, value })
  }
  pendingAffectionUpdates.clear()
}

export const useMarasStore = create<MarasState>((set, get) => ({
  isOpen: true,
  isInGame: isGameBuild,
  statusMsg: isGameBuild ? 'M.A.R.A.S Core Active' : 'Prisma UI Dev Mode',
  activeTab: 'overview',
  activeSubSection: 'dashboard',

  // NPC Detail Screen State
  npcDetailId: null,
  npcDetailPreviousContext: null,
  npcDetailPartnerId: null,
  evaluatingChildId: null,

  // Matchmaking Modal State
  matchmakingModalOpen: false,
  matchmakingTargetNpcId: null,
  matchmakingScanType: 'target',
  aiSuggestions: [],
  isGeneratingAiSuggestions: false,

  selectedNpcId: null,
  selectedNpcRelationsId: PLAYER_FORM_ID,
  matchmakingNpcA: null,
  matchmakingNpcB: null,
  detailPanelOpen: false,

  searchQueryA: '',
  searchQueryB: '',
  searchQueryModal: '',
  searchResultsA: [],
  searchResultsB: [],
  searchResultsModal: [],
  isSearchingA: false,
  isSearchingB: false,
  isSearchingModal: false,

  allSkyrimNpcs: isGameBuild ? [] : mockAllSkyrimNpcs,
  isLoadingAllNpcs: false,

  currentGameDay: isGameBuild ? 0 : 60.5,
  npcProfiles: initialNpcMap,
  relationships: isGameBuild ? [] : mockRelationships,
  children: initialChildMap,
  cheatingRecords: isGameBuild ? [] : mockCheatingRecords,
  affectionData: initialAffectionMap,
  hierarchyRanks: isGameBuild ? ['', '', ''] : mockHierarchyRanks,
  playerHomes: isGameBuild ? [] : mockPlayerHomes,
  settings: mockSettings,
  matchmakingSettings: {
    enabled: mockSettings.matchmakingEnabled ?? true,
    intervalDays: mockSettings.matchmakingIntervalDays ?? 5,
    maxSuggestions: mockSettings.matchmakingMaxSuggestions ?? 5,
    npcCooldownDays: mockSettings.matchmakingNpcCooldownDays ?? 14,
    allowBreakups: mockSettings.matchmakingAllowBreakups ?? true,
    allowOpenClose: mockSettings.matchmakingAllowOpenClose ?? true,
    allowCheating: mockSettings.matchmakingAllowCheating ?? true,
    orientation: (mockSettings.matchmakingOrientation as MatchmakingOrientation) ?? MatchmakingOrientation.Heterosexual,
    excludePlayerActivePartners: mockSettings.matchmakingExcludePlayerActivePartners ?? false,
    useLLM: true,
    generateMemories: true,
    evaluationStep: true,
    diceRollChanceNewDating: 22,
    diceRollChanceNewEngaged: 17,
    diceRollChanceNewLoverAllowed: 17,
    diceRollChanceNewLoverCheating: 6,
    diceRollChanceAdvance: 22,
    diceRollChanceBreakup: 11,
    diceRollChanceOpenClose: 5,
  },

  householdBuffs: isGameBuild ? null : mockHouseholdBuffs,
  isLoadingBuffs: false,
  setHouseholdBuffs: (buffs: HouseholdBuffsData | null) => set({ householdBuffs: buffs, isLoadingBuffs: false }),
  fetchHouseholdBuffs: () => {
    if (checkIsInGame()) {
      set({ isLoadingBuffs: true })
      sendMarasAction('fetchHouseholdBuffs')
    } else {
      set({ householdBuffs: mockHouseholdBuffs, isLoadingBuffs: false })
    }
  },

  isDumpingData: false,
  lastDumpResult: null,
  isDumpingNPCIndex: false,
  lastNPCIndexDumpResult: null,

  // NPC Detail Navigation
  openNpcDetail: (npcId: string) => {
    const { activeTab, activeSubSection, npcDetailId } = get()
    if (!npcDetailId) {
      set({
        npcDetailPreviousContext: { tab: activeTab, subSection: activeSubSection },
      })
    }
    sendMarasAction('fetchNPCDetail', { formId: npcId })
    set({
      npcDetailId: npcId,
      npcDetailPartnerId: null,
      selectedNpcId: npcId,
    })
  },

  closeNpcDetail: () => {
    const { npcDetailPreviousContext } = get()
    if (npcDetailPreviousContext) {
      set({
        activeTab: npcDetailPreviousContext.tab,
        activeSubSection: npcDetailPreviousContext.subSection,
      })
    }
    set({
      npcDetailId: null,
      npcDetailPartnerId: null,
      npcDetailPreviousContext: null,
      selectedNpcId: null,
    })
  },

  setNpcDetailPartnerId: (partnerId: string | null) => {
    if (partnerId) {
      sendMarasAction('fetchNPCDetail', { formId: partnerId })
    }
    set({ npcDetailPartnerId: partnerId })
  },

  // Matchmaking Modal & AI Suggestions Actions
  setMatchmakingModalOpen: (open: boolean, targetNpcId = null) => {
    set({
      matchmakingModalOpen: open,
      matchmakingTargetNpcId: targetNpcId || get().npcDetailId,
      matchmakingScanType: 'target',
      aiSuggestions: open ? get().aiSuggestions : [],
      searchResultsModal: [],
      isSearchingModal: false,
      searchQueryModal: '',
    })
  },

  setMatchmakingScanType: (type: 'target' | 'global') => {
    set({ matchmakingScanType: type })
  },

  setAiSuggestions: (suggestions: AiMatchSuggestion[]) => {
    set({ aiSuggestions: suggestions, isGeneratingAiSuggestions: false })
  },

  setMatchmakingSettings: (settings: Partial<MatchmakingSettings>) => {
    set((state) => ({
      matchmakingSettings: { ...state.matchmakingSettings, ...settings },
    }))
  },

  updateMatchmakingSettings: (settings: Partial<MatchmakingSettings>) => {
    // 1. Update React state immediately for responsive, fluid UI interaction
    set((state) => ({
      matchmakingSettings: { ...state.matchmakingSettings, ...settings },
    }))

    // 2. Accumulate pending updates
    pendingMatchmakingSettingsUpdates = {
      ...pendingMatchmakingSettingsUpdates,
      ...settings,
    }

    // 3. Debounce dispatching action to C++ bridge (300ms)
    if (matchmakingSettingsDebounceTimer) {
      clearTimeout(matchmakingSettingsDebounceTimer)
    }

    matchmakingSettingsDebounceTimer = setTimeout(() => {
      if (matchmakingSettingsDebounceTimer) {
        matchmakingSettingsDebounceTimer = null
      }
      if (Object.keys(pendingMatchmakingSettingsUpdates).length > 0) {
        const updates = { ...pendingMatchmakingSettingsUpdates }
        pendingMatchmakingSettingsUpdates = {}
        sendMarasAction('updateMatchmakingSettings', updates)
      }
    }, 300)
  },

  generateAiSuggestions: (npcId: string) => {
    const { settings, isInGame } = get()
    // Explicitly check SkyrimNet availability
    if (!settings.hasSkyrimNet) return

    // Ensure latest settings are pushed to backend before scan starts
    flushAllPendingDebouncedActions()

    set({ isGeneratingAiSuggestions: true, aiSuggestions: [], matchmakingScanType: 'target', matchmakingTargetNpcId: npcId })
    sendMarasAction('skyrimNetMatchSuggest', { formId: npcId })

    if (!isInGame) {
      // Mock AI suggestions for dev standalone mode
      setTimeout(() => {
        const { npcProfiles, matchmakingSettings } = get()
        const targetProfile = npcProfiles.get(npcId)
        const targetGender = targetProfile?.gender ?? 'male'
        const orientation = matchmakingSettings.orientation ?? MatchmakingOrientation.Heterosexual

        let available = Array.from(npcProfiles.values()).filter(
          (n) => n.formId !== npcId && n.formId !== PLAYER_FORM_ID,
        )

        if (orientation === MatchmakingOrientation.Heterosexual) {
          available = available.filter((n) => (n.gender ?? 'male') !== targetGender)
        } else if (orientation === MatchmakingOrientation.Homosexual) {
          available = available.filter((n) => (n.gender ?? 'male') === targetGender)
        } else if (orientation === MatchmakingOrientation.Gay) {
          available = targetGender === 'male' ? available.filter((n) => (n.gender ?? 'male') === 'male') : []
        } else if (orientation === MatchmakingOrientation.Lesbian) {
          available = targetGender === 'female' ? available.filter((n) => (n.gender ?? 'male') === 'female') : []
        } else if (orientation === MatchmakingOrientation.HeteroAndGay) {
          if (targetGender === 'female') {
            available = available.filter((n) => (n.gender ?? 'male') === 'male')
          }
        } else if (orientation === MatchmakingOrientation.HeteroAndLesbian) {
          if (targetGender === 'male') {
            available = available.filter((n) => (n.gender ?? 'male') === 'female')
          }
        }
        const mockReasonings = [
          'SkyrimNet AI context reveals exceptional personality harmony & shared social values. High romantic potential.',
          'Strong alignment in skill specialization and faction lore. High affection growth multiplier.',
          'Favorable temperament compatibility. Shared background provides solid relationship foundation.',
        ]
        const mockMemories = [
          'They shared stories over mead at the local tavern, discovering a mutual passion for adventuring.',
          'After fighting side by side against bandits, a deep bond of trust and affection began to blossom.',
          'A quiet moment in the temple of Mara sparked conversations about the future and shared dreams.',
        ]
        const mockTags = [
          ['#RomanticSynergy', '#SharedValues', '#HighAffection'],
          ['#SkillHarmony', '#LoreCompatible', '#HighLoyalty'],
          ['#TemperamentMatch', '#GrowingAffinity', '#StablePair'],
        ]
        const mockScores = [96, 88, 79]

        const mockResults: AiMatchSuggestion[] = available.slice(0, 3).map((npc, idx) => ({
          formId: npc.formId,
          name: npc.name,
          action: idx === 0 ? 'new' : 'advance',
          targetStatus: idx === 0 ? 'dating' : 'lover',
          suggestedStatus: idx === 0 ? RelationshipStatus.Dating : RelationshipStatus.Lover,
          selected: true,
          compatibilityScore: mockScores[idx % mockScores.length],
          reasoning: mockReasonings[idx % mockReasonings.length],
          memory: mockMemories[idx % mockMemories.length],
          synergyTags: mockTags[idx % mockTags.length],
        }))
        set({ aiSuggestions: mockResults, isGeneratingAiSuggestions: false })
      }, 800)
    } else {
      // Safety timeout: clear spinner if SkyrimNet fails to respond within 30s
      setTimeout(() => {
        if (get().isGeneratingAiSuggestions) {
          console.warn('[MarasStore] Matchmaking request timed out after 30s')
          set({ isGeneratingAiSuggestions: false })
          get().setStatusMsg('Matchmaking request timed out or returned no response.')
        }
      }, 30000)
    }
  },

  generateGlobalAiSuggestions: (autoApply = false) => {
    const { settings, isInGame } = get()
    if (!settings.hasSkyrimNet) {
      set({ isGeneratingAiSuggestions: false })
      get().setStatusMsg('SkyrimNet AI is not available.')
      return
    }

    // Ensure latest settings are pushed to backend before scan starts
    flushAllPendingDebouncedActions()

    set({ isGeneratingAiSuggestions: true, aiSuggestions: [], matchmakingScanType: 'global' })
    sendMarasAction('skyrimNetGlobalMatchSuggest', { autoApply })

    if (isInGame) {
      setTimeout(() => {
        if (get().isGeneratingAiSuggestions) {
          console.warn('[MarasStore] Global matchmaking scan timed out after 45s')
          set({ isGeneratingAiSuggestions: false })
          get().setStatusMsg('Global matchmaking scan timed out or returned no response.')
        }
      }, 45000)
    } else {
      setTimeout(() => {
        const { allSkyrimNpcs } = get()
        const eligible = allSkyrimNpcs.filter((n) => !n.isChild && n.formId !== PLAYER_FORM_ID)
        const mockResults: AiMatchSuggestion[] = []
        if (eligible.length >= 2) {
          mockResults.push({
            formId: eligible[1].formId,
            name: eligible[1].name,
            actorAFormId: eligible[0].formId,
            actorAName: eligible[0].name,
            action: 'new',
            targetStatus: 'dating',
            suggestedStatus: RelationshipStatus.Dating,
            selected: true,
            compatibilityScore: 92,
            reasoning: 'Both frequent the market and have compatible trade backgrounds in Skyrim.',
            memory: 'Met during a festival in the town square and bonded over shared stories.',
            synergyTags: ['#WorldScan', '#RomanticSynergy'],
          })
        }
        set({ aiSuggestions: mockResults, isGeneratingAiSuggestions: false })
      }, 800)
    }
  },

  generateMatchMemories: (targetFormId: string, partnerFormId: string, targetStatus: string, action?: string) => {
    const { settings, isInGame } = get()
    if (!settings.hasSkyrimNet) return

    set((state) => ({
      aiSuggestions: state.aiSuggestions.map((s) =>
        s.formId === partnerFormId ? { ...s, isGeneratingMemory: true } : s,
      ),
    }))

    sendMarasAction('generateMatchMemories', {
      targetFormId,
      partnerFormId,
      targetStatus,
      action: action || 'new',
    })

    if (isInGame) {
      // Safety timeout: clear memory spinner if response is not received within 30s
      setTimeout(() => {
        set((state) => ({
          aiSuggestions: state.aiSuggestions.map((s) =>
            s.formId === partnerFormId && s.isGeneratingMemory
              ? { ...s, isGeneratingMemory: false }
              : s,
          ),
        }))
      }, 30000)
    }

    if (!isInGame) {
      setTimeout(() => {
        const { npcProfiles } = get()
        const nameA = npcProfiles.get(targetFormId)?.name || 'Target'
        const nameB = npcProfiles.get(partnerFormId)?.name || 'Partner'
        const mockDual: NpcMemoryEntry[] = [
          {
            actor: nameA,
            actorFormId: targetFormId,
            content: `I spoke with ${nameB} at length in Skyrim. A mutual understanding and deep affection began taking root between us.`,
            location: 'The Bannered Mare in Whiterun',
            emotion: 'joyful',
            importance_score: 0.75,
            tags: [nameA.toLowerCase(), nameB.toLowerCase(), 'romance', targetStatus.toLowerCase()],
            type: 'RELATIONSHIP',
          },
          {
            actor: nameB,
            actorFormId: partnerFormId,
            content: `Had a memorable encounter with ${nameA} in Whiterun. Their presence brought warmth and the promise of a shared future.`,
            location: 'The Bannered Mare in Whiterun',
            emotion: 'content',
            importance_score: 0.75,
            tags: [nameA.toLowerCase(), nameB.toLowerCase(), 'romance', targetStatus.toLowerCase()],
            type: 'RELATIONSHIP',
          },
        ]
        get().receiveGeneratedMemories({ partnerFormId, memories: mockDual })
      }, 800)
    }
  },

  receiveGeneratedMemories: (data: { partnerFormId: string; memories: NpcMemoryEntry[] }) => {
    set((state) => ({
      aiSuggestions: state.aiSuggestions.map((s) =>
        s.formId === data.partnerFormId
          ? { ...s, dualMemories: data.memories, isGeneratingMemory: false }
          : s,
      ),
    }))
  },

  toggleAiSuggestionSelected: (formId: string) => {
    set((state) => ({
      aiSuggestions: state.aiSuggestions.map((s) =>
        s.formId === formId ? { ...s, selected: !s.selected } : s,
      ),
    }))
  },

  updateAiSuggestionStatus: (formId: string, status: RelationshipStatus) => {
    set((state) => ({
      aiSuggestions: state.aiSuggestions.map((s) =>
        s.formId === formId ? { ...s, suggestedStatus: status } : s,
      ),
    }))
  },

  applyAiSuggestions: (npcId?: string) => {
    const { aiSuggestions, assignRelationshipStatus, setPairIsOpen } = get()
    const selected = aiSuggestions.filter((s) => s.selected)
    selected.forEach((s) => {
      const primaryActor = s.actorAFormId || npcId
      if (!primaryActor || !s.formId) return
      if (s.action === 'open') {
        setPairIsOpen(primaryActor, s.formId, true, true)
      } else if (s.action === 'close') {
        setPairIsOpen(primaryActor, s.formId, false, true)
      } else {
        assignRelationshipStatus(primaryActor, s.formId, s.suggestedStatus, s.dualMemories, true)
      }
    })
    set({ matchmakingModalOpen: false, aiSuggestions: [] })
  },

  // Teleport & Child Companion Actions
  teleportPlayerToNpc: (npcId: string) => {
    sendMarasAction('teleportPlayerToNpc', { formId: npcId })
    set({ statusMsg: `Teleporting Player to NPC (${npcId})...` })
  },

  teleportNpcToPlayer: (npcId: string) => {
    sendMarasAction('teleportNpcToPlayer', { formId: npcId })
    set({ statusMsg: `Teleporting NPC (${npcId}) to Player...` })
  },

  fleeChildrenFromCell: () => {
    sendMarasAction('fleeChildrenFromCell', {})
    set({ statusMsg: 'Children are running away from this cell...' })
  },

  dismissChildCritter: (npcId: string) => {
    sendMarasAction('dismissChildCritter', { formId: npcId })
    set({ statusMsg: `Dismissing critter for child (${npcId})...` })
  },

  dismissChildPet: (npcId: string) => {
    sendMarasAction('dismissChildPet', { formId: npcId })
    set({ statusMsg: `Dismissing pet for child (${npcId})...` })
  },

  spawnChild: (childId: string) => {
    sendMarasAction('spawnChild', { childId })
    set({ statusMsg: `Spawning child actor (${childId})...` })

    if (typeof window !== 'undefined' && !window.marasAction) {
      setTimeout(() => {
        set((state) => {
          const newChildren = new Map(state.children)
          const child = newChildren.get(childId)
          const newProfiles = new Map(state.npcProfiles)
          const profile = newProfiles.get(childId)

          if (child) {
            newChildren.set(childId, {
              ...child,
              hasRefID: true,
              isEphemeral: false,
            })
          }
          if (profile) {
            newProfiles.set(childId, {
              ...profile,
              hasRefID: true,
              isEphemeral: false,
            })
          }

          return {
            children: newChildren,
            npcProfiles: newProfiles,
            statusMsg: `Child (${childId}) successfully spawned into the world!`,
          }
        })
      }, 300)
    }
  },

  handleChildPromoted: (oldId: string, newId: string) => {
    set((state) => {
      const newChildren = new Map(state.children)
      const oldChild = newChildren.get(oldId)
      if (oldChild) {
        newChildren.delete(oldId)
        newChildren.set(newId, {
          ...oldChild,
          childId: newId,
          hasRefID: true,
          isEphemeral: false,
        })
      }
      const newProfiles = new Map(state.npcProfiles)
      const oldProf = newProfiles.get(oldId)
      if (oldProf) {
        newProfiles.delete(oldId)
        newProfiles.set(newId, {
          ...oldProf,
          formId: newId,
          hasRefID: true,
          isEphemeral: false,
        })
      }
      return {
        children: newChildren,
        npcProfiles: newProfiles,
        npcDetailId: state.npcDetailId === oldId ? newId : state.npcDetailId,
        selectedNpcId: state.selectedNpcId === oldId ? newId : state.selectedNpcId,
        statusMsg: `Child spawned successfully.`,
      }
    })
    sendMarasAction('fetchNPCDetail', { formId: newId })
    sendMarasAction('fetchOverview')
  },

  evaluateChildSecret: (childId: string) => {
    sendMarasAction('evaluateChildSecret', { childId })
    set({
      evaluatingChildId: childId,
      statusMsg: `Evaluating paternity secret for child (${childId})...`,
    })

    // Fallback in case LLM or backend takes long or is running standalone
    if (typeof window !== 'undefined' && !window.marasAction) {
      setTimeout(() => {
        set((state) => {
          const newChildren = new Map(state.children)
          const target = newChildren.get(childId)
          if (target) {
            newChildren.set(childId, {
              ...target,
              isSecret: false,
              isSecretForBioFather: false,
              isSecretForPartner: false,
            })
          }
          return {
            children: newChildren,
            evaluatingChildId: null,
            statusMsg: `Secret evaluated for child (${childId}).`,
          }
        })
      }, 1500)
    } else {
      setTimeout(() => {
        if (get().evaluatingChildId === childId) {
          set({ evaluatingChildId: null })
        }
      }, 30000)
    }
  },

  setStatusMsg: (statusMsg: string) => set({ statusMsg }),
  setIsOpen: (isOpen: boolean) => {
    set({ isOpen })
  },
  setPlayerHomes: (playerHomes: PlayerHouse[]) => set({ playerHomes }),

  setAllSkyrimNpcs: (allSkyrimNpcs) => set({ allSkyrimNpcs, isLoadingAllNpcs: false }),

  fetchAllSkyrimNpcs: () => {
    const { isInGame } = get()
    set({ isLoadingAllNpcs: true })
    if (isInGame) {
      sendMarasAction('fetchAllSkyrimNpcs')
    } else {
      setTimeout(() => {
        set({ allSkyrimNpcs: mockAllSkyrimNpcs, isLoadingAllNpcs: false })
      }, 200)
    }
  },

  setSearchQuery: (side, query) => {
    const trimmed = query.trim()
    const { isInGame, npcProfiles } = get()

    if (side === 'a') {
      set({ searchQueryA: query })
      if (trimmed.length >= 3) {
        set({ isSearchingA: true })
        if (isInGame) {
          sendMarasAction('searchNPCs', { query: trimmed, side: 'A' })
        } else {
          setTimeout(() => {
            const results = Array.from(npcProfiles.values()).filter(
              (n) =>
                n.name.toLowerCase().includes(trimmed.toLowerCase()) ||
                n.race.toLowerCase().includes(trimmed.toLowerCase()) ||
                n.formId.toLowerCase().includes(trimmed.toLowerCase()) ||
                (n.pluginName && n.pluginName.toLowerCase().includes(trimmed.toLowerCase())),
            )
            get().setSearchResults('a', results)
          }, 200)
        }
      } else {
        set({ isSearchingA: false, searchResultsA: [] })
      }
    } else if (side === 'b') {
      set({ searchQueryB: query })
      if (trimmed.length >= 3) {
        set({ isSearchingB: true })
        if (isInGame) {
          sendMarasAction('searchNPCs', { query: trimmed, side: 'B' })
        } else {
          setTimeout(() => {
            const results = Array.from(npcProfiles.values()).filter(
              (n) =>
                n.name.toLowerCase().includes(trimmed.toLowerCase()) ||
                n.race.toLowerCase().includes(trimmed.toLowerCase()) ||
                n.formId.toLowerCase().includes(trimmed.toLowerCase()) ||
                (n.pluginName && n.pluginName.toLowerCase().includes(trimmed.toLowerCase())),
            )
            get().setSearchResults('b', results)
          }, 200)
        }
      } else {
        set({ isSearchingB: false, searchResultsB: [] })
      }
    } else if (side === 'modal') {
      set({ searchQueryModal: query })
      if (trimmed.length >= 3) {
        set({ isSearchingModal: true })
        if (isInGame) {
          sendMarasAction('searchNPCs', { query: trimmed, side: 'MODAL' })
        } else {
          setTimeout(() => {
            const results = Array.from(npcProfiles.values()).filter(
              (n) =>
                n.name.toLowerCase().includes(trimmed.toLowerCase()) ||
                n.race.toLowerCase().includes(trimmed.toLowerCase()) ||
                n.formId.toLowerCase().includes(trimmed.toLowerCase()) ||
                (n.pluginName && n.pluginName.toLowerCase().includes(trimmed.toLowerCase())),
            )
            get().setSearchResults('modal', results)
          }, 200)
        }
      } else {
        set({ isSearchingModal: false, searchResultsModal: [] })
      }
    }
  },

  setSearchResults: (side, results) => {
    set((state) => {
      const newNpcProfiles = new Map(state.npcProfiles)

      const profiles: NPCProfile[] = results.map((item) => {
        const existing = newNpcProfiles.get(item.formId)
        const profile: NPCProfile = {
          formId: item.formId,
          name: item.name || existing?.name || `NPC ${item.formId}`,
          race: item.race || existing?.race || 'Nord',
          pluginName: item.pluginName || existing?.pluginName,
          socialClass: existing?.socialClass ?? item.socialClass ?? SocialClass.Middle,
          skillType: existing?.skillType ?? item.skillType ?? SkillType.Warrior,
          temperament: existing?.temperament ?? item.temperament ?? Temperament.Proud,
          currentHome: existing?.currentHome ?? item.currentHome,
          originalHome: existing?.originalHome ?? item.originalHome,
          isChild: existing?.isChild ?? item.isChild,
        }

        newNpcProfiles.set(item.formId, profile)
        return profile
      })

      if (side === 'a') {
        return { searchResultsA: profiles, isSearchingA: false, npcProfiles: newNpcProfiles }
      } else if (side === 'b') {
        return { searchResultsB: profiles, isSearchingB: false, npcProfiles: newNpcProfiles }
      } else {
        return { searchResultsModal: profiles, isSearchingModal: false, npcProfiles: newNpcProfiles }
      }
    })
  },

  setActiveTab: (activeTab: string) => {
    let defaultSub: string | null = null
    if (activeTab === 'overview') defaultSub = 'dashboard'
    else if (activeTab === 'household' || activeTab === 'partners') defaultSub = 'all'
    else if (activeTab === 'matchmaking') defaultSub = 'manualMatch'
    else if (activeTab === 'settings') defaultSub = 'general'

    if (activeTab === 'overview' || activeTab === 'household' || activeTab === 'partners' || activeTab === 'matchmaking') {
      sendMarasAction('fetchNPCList')
    }

    set({
      activeTab,
      activeSubSection: defaultSub,
      npcDetailId: null,
      npcDetailPartnerId: null,
      npcDetailPreviousContext: null,
      selectedNpcId: null,
    })
  },

  setActiveSubSection: (activeSubSection: string | null) =>
    set({
      activeSubSection,
      npcDetailId: null,
      npcDetailPartnerId: null,
      npcDetailPreviousContext: null,
      selectedNpcId: null,
    }),

  setSelectedNpcId: (id: string | null) => {
    if (id) {
      sendMarasAction('fetchNPCDetail', { formId: id })
    }
    set({ selectedNpcId: id, detailPanelOpen: id !== null })
  },

  setSelectedNpcRelationsId: (id: string | null) => set({ selectedNpcRelationsId: id }),
  setMatchmakingNpcA: (id: string | null) => set({ matchmakingNpcA: id }),
  setMatchmakingNpcB: (id: string | null) => set({ matchmakingNpcB: id }),
  setDetailPanelOpen: (detailPanelOpen: boolean) => set({ detailPanelOpen }),

  setLiveGameData: (data) => {
    set((state) => {
      const updates: Partial<MarasState> = {}

      if (data.currentGameDay != null) {
        updates.currentGameDay = data.currentGameDay
      }

      if (data.npcProfiles && Array.isArray(data.npcProfiles)) {
        const newMap = new Map(state.npcProfiles)
        data.npcProfiles.forEach((npc) => {
          const existing = newMap.get(npc.formId)
          newMap.set(npc.formId, existing ? { ...existing, ...npc } : npc)
        })
        updates.npcProfiles = newMap
      }

      if (data.relationships && Array.isArray(data.relationships)) {
        updates.relationships = data.relationships
      }

      if (data.children && Array.isArray(data.children)) {
        const newMap = new Map(state.children)
        data.children.forEach((c) => {
          const existing = newMap.get(c.childId)
          newMap.set(c.childId, existing ? { ...existing, ...c } : c)
        })
        updates.children = newMap
      }

      if (data.cheatingRecords && Array.isArray(data.cheatingRecords)) {
        updates.cheatingRecords = data.cheatingRecords
      }

      if (data.affectionData && Array.isArray(data.affectionData)) {
        const newMap = new Map(state.affectionData)
        data.affectionData.forEach((aff) => newMap.set(aff.formId, aff))
        updates.affectionData = newMap
      }

      if (data.hierarchyRanks) {
        updates.hierarchyRanks = data.hierarchyRanks
      }

      if (data.playerHomes && Array.isArray(data.playerHomes)) {
        updates.playerHomes = data.playerHomes
      }

      if (data.settings) {
        updates.settings = { ...state.settings, ...data.settings }
        if (data.settings.matchmakingExcludePlayerActivePartners !== undefined) {
          updates.matchmakingSettings = {
            ...(updates.matchmakingSettings ?? state.matchmakingSettings),
            excludePlayerActivePartners: data.settings.matchmakingExcludePlayerActivePartners,
          }
        }
      }

      return updates
    })
  },

  updateAffection: (formId: string, value: number, skipBridge = false) => {
    const clampedVal = Math.max(0, Math.min(100, value))
    const existing = get().affectionData.get(formId)

    if (existing && existing.permanent === clampedVal) {
      return
    }

    set((state) => {
      const newMap = new Map(state.affectionData)
      const prev = newMap.get(formId) || {
        formId,
        permanent: clampedVal,
        decayMultiplier: 1.0,
        daysSinceLastInteraction: 0,
      }
      newMap.set(formId, { ...prev, permanent: clampedVal })
      return { affectionData: newMap }
    })

    const existingTimer = setAffectionDebounceTimers.get(formId)
    if (existingTimer) {
      clearTimeout(existingTimer)
      setAffectionDebounceTimers.delete(formId)
    }

    if (!skipBridge) {
      pendingAffectionUpdates.set(formId, clampedVal)
      const timer = setTimeout(() => {
        sendMarasAction('setAffection', { formId, value: clampedVal })
        setAffectionDebounceTimers.delete(formId)
        pendingAffectionUpdates.delete(formId)
      }, 300)
      setAffectionDebounceTimers.set(formId, timer)
    }
  },

  updateNPCProfile: (formId: string, updates: Partial<NPCProfile>, skipBridge = false) => {
    const existing = get().npcProfiles.get(formId)

    const isSameValue = (valA: any, valB: any): boolean => {
      if (valA === valB) return true
      if (typeof valA === 'object' && valA !== null && typeof valB === 'object' && valB !== null) {
        return JSON.stringify(valA) === JSON.stringify(valB)
      }
      return false
    }

    const hasChanges =
      !existing ||
      (Object.keys(updates) as (keyof NPCProfile)[]).some((key) => {
        const newVal = updates[key]
        if (newVal === undefined) return false
        return !isSameValue(newVal, existing[key])
      })

    if (!hasChanges) {
      return
    }

    if (!skipBridge) {
      if (updates.socialClass !== undefined && !isSameValue(updates.socialClass, existing?.socialClass)) {
        sendMarasAction('setSocialClass', { formId, socialClass: updates.socialClass })
      }
      if (updates.skillType !== undefined && !isSameValue(updates.skillType, existing?.skillType)) {
        sendMarasAction('setSkillType', { formId, skillType: updates.skillType })
      }
      if (updates.temperament !== undefined && !isSameValue(updates.temperament, existing?.temperament)) {
        sendMarasAction('setTemperament', { formId, temperament: updates.temperament })
      }
      if (updates.critterBanned !== undefined && !isSameValue(updates.critterBanned, existing?.critterBanned)) {
        sendMarasAction('setChildCritterBanned', { formId, banned: updates.critterBanned })
      }
      if (updates.petBanned !== undefined && !isSameValue(updates.petBanned, existing?.petBanned)) {
        sendMarasAction('setChildPetBanned', { formId, banned: updates.petBanned })
      }
      if (updates.currentHome !== undefined && !isSameValue(updates.currentHome, existing?.currentHome)) {
        sendMarasAction('setNPCCurrentHome', { formId, home: updates.currentHome })
      }
      if (updates.stayAtHome !== undefined && !isSameValue(updates.stayAtHome, existing?.stayAtHome)) {
        sendMarasAction('setNPCStayAtHome', { formId, stayAtHome: updates.stayAtHome })
      }
    }

    set((state) => {
      const newMap = new Map(state.npcProfiles)
      const currentExisting = newMap.get(formId)
      const base: NPCProfile = currentExisting || {
        formId,
        name: updates.name || `NPC ${formId}`,
        race: updates.race || 'Nord',
        socialClass: SocialClass.Middle,
        skillType: SkillType.Warrior,
        temperament: Temperament.Proud,
      }
      newMap.set(formId, { ...base, ...updates })

      // Reactive family unit cascade when currentHome changes:
      if (updates.currentHome !== undefined) {
        const targetHome = updates.currentHome
        const childRec = state.children.get(formId)

        if (childRec) {
          // formId is a child: update their specific co-parent spouse and all siblings sharing this spouse
          const coParentId = areSameFormId(childRec.guardianAId, PLAYER_FORM_ID)
            ? childRec.guardianBId
            : (areSameFormId(childRec.guardianBId, PLAYER_FORM_ID) ? childRec.guardianAId : undefined)

          if (coParentId && !areSameFormId(coParentId, PLAYER_FORM_ID)) {
            const spouseProf = newMap.get(coParentId)
            if (spouseProf) {
              newMap.set(coParentId, { ...spouseProf, currentHome: targetHome })
            }

            for (const c of state.children.values()) {
              if (
                !areSameFormId(c.childId, formId) &&
                ((areSameFormId(c.guardianAId, PLAYER_FORM_ID) && areSameFormId(c.guardianBId, coParentId)) ||
                  (areSameFormId(c.guardianBId, PLAYER_FORM_ID) && areSameFormId(c.guardianAId, coParentId)))
              ) {
                const sibProf = newMap.get(c.childId)
                if (sibProf) {
                  newMap.set(c.childId, { ...sibProf, currentHome: targetHome })
                }
              }
            }
          }
        } else {
          // formId is a spouse: update all children assigned to (PLAYER_FORM_ID, formId)
          for (const c of state.children.values()) {
            if (
              (areSameFormId(c.guardianAId, PLAYER_FORM_ID) && areSameFormId(c.guardianBId, formId)) ||
              (areSameFormId(c.guardianBId, PLAYER_FORM_ID) && areSameFormId(c.guardianAId, formId)) ||
              (areSameFormId(c.bioMotherId, formId) && areSameFormId(c.bioFatherId, PLAYER_FORM_ID)) ||
              (areSameFormId(c.bioFatherId, formId) && areSameFormId(c.bioMotherId, PLAYER_FORM_ID))
            ) {
              const childProf = newMap.get(c.childId)
              if (childProf) {
                newMap.set(c.childId, { ...childProf, currentHome: targetHome })
              }
            }
          }
        }
      }

      return { npcProfiles: newMap }
    })
  },

  setPairIsOpen: (actorA: string, actorB: string, isOpen: boolean, fromMatchmaking?: boolean) => {
    sendMarasAction('setPairIsOpen', { actorA, actorB, isOpen, fromMatchmaking })
    set((state) => {
      const newRels = state.relationships.map((r) => {
        if (
          (r.actorA === actorA && r.actorB === actorB) ||
          (r.actorA === actorB && r.actorB === actorA)
        ) {
          return { ...r, isOpen }
        }
        return r
      })
      return { relationships: newRels }
    })
  },

  setHierarchyRank: (formId: string, rankIndex: number) => {
    sendMarasAction('setHierarchyRank', { formId, rank: rankIndex })
    set((state) => {
      const ranks: [string, string, string] = [...state.hierarchyRanks] as [string, string, string]
      const existingIndex = ranks.indexOf(formId)
      if (existingIndex !== -1) ranks[existingIndex] = ''

      if (rankIndex >= 0 && rankIndex < 3) {
        ranks[rankIndex] = formId
      }
      return { hierarchyRanks: ranks }
    })
  },

  assignRelationshipStatus: (actorA: string, actorB: string, status: RelationshipStatus, dualMemories?: NpcMemoryEntry[], fromMatchmaking?: boolean) => {
    sendMarasAction('assignRelationship', { actorA, actorB, status, dualMemories, fromMatchmaking })
    set((state) => {
      const pairIndex = state.relationships.findIndex(
        (r) =>
          (areSameFormId(r.actorA, actorA) && areSameFormId(r.actorB, actorB)) ||
          (areSameFormId(r.actorA, actorB) && areSameFormId(r.actorB, actorA)),
      )

      const newRelationships = [...state.relationships]
      if (pairIndex !== -1) {
        const oldPair = newRelationships[pairIndex]
        newRelationships[pairIndex] = {
          ...oldPair,
          status,
          statusHistory: [
            ...oldPair.statusHistory,
            {
              fromStatus: oldPair.status,
              toStatus: status,
              timestamp: state.currentGameDay,
              initiatorFormId: PLAYER_FORM_ID,
            },
          ],
        }
      } else {
        newRelationships.push({
          actorA,
          actorB,
          status,
          isOpen: false,
          statusHistory: [
            {
              fromStatus: RelationshipStatus.Candidate,
              toStatus: status,
              timestamp: state.currentGameDay,
              initiatorFormId: PLAYER_FORM_ID,
            },
          ],
        })
      }

      return { relationships: newRelationships }
    })
  },

  handleRealtimeStatusChanged: (actorA: string, actorB: string, status: RelationshipStatus, isOpen?: boolean) => {
    set((state) => {
      if (status === RelationshipStatus.Candidate || status === RelationshipStatus.Unknown) {
        return {
          relationships: state.relationships.filter(
            (r) =>
              !(
                (areSameFormId(r.actorA, actorA) && areSameFormId(r.actorB, actorB)) ||
                (areSameFormId(r.actorA, actorB) && areSameFormId(r.actorB, actorA))
              ),
          ),
        }
      }
      const pairIndex = state.relationships.findIndex(
        (r) =>
          (areSameFormId(r.actorA, actorA) && areSameFormId(r.actorB, actorB)) ||
          (areSameFormId(r.actorA, actorB) && areSameFormId(r.actorB, actorA)),
      )
      const newRelationships = [...state.relationships]
      if (pairIndex !== -1) {
        const oldPair = newRelationships[pairIndex]
        newRelationships[pairIndex] = {
          ...oldPair,
          status,
          ...(isOpen !== undefined ? { isOpen } : {}),
          statusHistory: [
            ...oldPair.statusHistory,
            {
              fromStatus: oldPair.status,
              toStatus: status,
              timestamp: state.currentGameDay,
              initiatorFormId: PLAYER_FORM_ID,
            },
          ],
        }
      } else {
        newRelationships.push({
          actorA,
          actorB,
          status,
          isOpen: isOpen ?? false,
          statusHistory: [
            {
              fromStatus: RelationshipStatus.Candidate,
              toStatus: status,
              timestamp: state.currentGameDay,
              initiatorFormId: PLAYER_FORM_ID,
            },
          ],
        })
      }
      return { relationships: newRelationships }
    })
  },

  removeRelationship: (actorA: string, actorB: string, fromMatchmaking?: boolean) => {
    sendMarasAction('removeRelationship', { actorA, actorB, fromMatchmaking })
    set((state) => {
      const isMatchingPair = (x: string, y: string) =>
        (areSameFormId(x, actorA) && areSameFormId(y, actorB)) ||
        (areSameFormId(x, actorB) && areSameFormId(y, actorA))

      const shouldClosePartnerDrawer =
        state.npcDetailId !== null &&
        state.npcDetailPartnerId !== null &&
        isMatchingPair(state.npcDetailId, state.npcDetailPartnerId)

      const playerPartnerId = areSameFormId(actorA, PLAYER_FORM_ID)
        ? actorB
        : areSameFormId(actorB, PLAYER_FORM_ID)
          ? actorA
          : null

      const nextRanks: [string, string, string] = playerPartnerId
        ? (state.hierarchyRanks.map((id) => (areSameFormId(id, playerPartnerId) ? '' : id)) as [
            string,
            string,
            string,
          ])
        : state.hierarchyRanks

      return {
        relationships: state.relationships.filter((r) => !isMatchingPair(r.actorA, r.actorB)),
        cheatingRecords: state.cheatingRecords.filter(
          (c) =>
            !isMatchingPair(c.cheaterId, c.affairPartnerId) &&
            !isMatchingPair(c.cheaterId, c.victimId),
        ),
        hierarchyRanks: nextRanks,
        ...(shouldClosePartnerDrawer ? { npcDetailPartnerId: null } : {}),
        statusMsg: 'Relationship record removed.',
      }
    })
  },

  addChild: (
    child: {
      childId: string
      guardianAId: string
      guardianBId?: string
      isAdopted?: boolean
      houseId?: string
      assignSpouseHome?: boolean
    },
    skipBridge = false
  ) => {
    if (!skipBridge) {
      sendMarasAction('addChild', child)
    }
    set((state) => {
      const newChildren = new Map(state.children)
      const childNpc = state.npcProfiles.get(child.childId)
      const guardianANpc = state.npcProfiles.get(child.guardianAId)
      const guardianBNpc = child.guardianBId ? state.npcProfiles.get(child.guardianBId) : undefined

      const record: ChildRecord = {
        childId: child.childId,
        name: childNpc?.name || `Child ${child.childId}`,
        timestamp: state.currentGameDay,
        guardianAId: child.guardianAId,
        guardianBId: child.guardianBId || undefined,
        guardianAName: guardianANpc?.name,
        guardianBName: guardianBNpc?.name,
        isAdopted: child.isAdopted ?? true,
        isBiologicalChild: !(child.isAdopted ?? true),
        stage: childNpc?.stage || 'child',
        gender: childNpc?.gender,
      }
      newChildren.set(child.childId, record)

      const newProfiles = new Map(state.npcProfiles)
      const targetHouse = child.houseId
        ? state.playerHomes.find((h) => h.formId === child.houseId || h.name === child.houseId)
        : undefined
      const houseName = targetHouse?.name || child.houseId

      if (houseName) {
        const existingChild = newProfiles.get(child.childId)
        if (existingChild) {
          newProfiles.set(child.childId, { ...existingChild, currentHome: houseName, isChild: true })
        }

        const spouseId = child.guardianAId === PLAYER_FORM_ID ? child.guardianBId : child.guardianAId
        if (spouseId && child.assignSpouseHome) {
          const existingSpouse = newProfiles.get(spouseId)
          if (existingSpouse) {
            newProfiles.set(spouseId, {
              ...existingSpouse,
              currentHome: houseName,
              stayAtHome: false,
            })
          }
        }
      }

      return { children: newChildren, npcProfiles: newProfiles }
    })
  },

  removeChild: (childId: string, skipBridge = false) => {
    if (!skipBridge) {
      sendMarasAction('removeChild', { childId })
    }
    set((state) => {
      const newChildren = new Map(state.children)
      newChildren.delete(childId)
      const newProfiles = new Map(state.npcProfiles)
      newProfiles.delete(childId)
      return {
        children: newChildren,
        npcProfiles: newProfiles,
        npcDetailId: state.npcDetailId === childId ? null : state.npcDetailId,
        selectedNpcId: state.selectedNpcId === childId ? null : state.selectedNpcId,
      }
    })
  },

  updateChild: (childId: string, updates: Partial<ChildRecord>, skipBridge = false) => {
    if (!skipBridge) {
      sendMarasAction('updateChild', { childId, ...updates })
    }
    set((state) => {
      const newChildren = new Map(state.children)
      const existing = newChildren.get(childId)
      if (existing) {
        newChildren.set(childId, { ...existing, ...updates })
      } else {
        newChildren.set(childId, updates as ChildRecord)
      }

      const newProfiles = new Map(state.npcProfiles)
      const existingProf = newProfiles.get(childId)
      if (existingProf) {
        newProfiles.set(childId, {
          ...existingProf,
          ...(updates.name ? { name: updates.name } : {}),
          ...(updates.stage ? { stage: updates.stage } : {}),
          ...(updates.hasRefID !== undefined ? { hasRefID: updates.hasRefID } : {}),
          ...(updates.isEphemeral !== undefined ? { isEphemeral: updates.isEphemeral } : {}),
        })
      }

      return { children: newChildren, npcProfiles: newProfiles }
    })
  },

  relocateFamilyUnit: ({ childId, home, coParentId, siblingIds }) => {
    sendMarasAction('relocateFamilyUnit', { childId, home, coParentId, siblingIds })
    set((state) => {
      const newProfiles = new Map(state.npcProfiles)
      const updateHome = (id: string, stayAtHome?: boolean) => {
        const prof = newProfiles.get(id)
        if (prof) {
          newProfiles.set(id, {
            ...prof,
            currentHome: home,
            ...(stayAtHome !== undefined ? { stayAtHome } : {}),
          })
        }
      }
      updateHome(childId)
      if (coParentId) updateHome(coParentId, false)
      if (siblingIds) {
        siblingIds.forEach((sId) => updateHome(sId))
      }
      return {
        npcProfiles: newProfiles,
        statusMsg: `Relocated family unit to ${home}`,
      }
    })
  },

  setPaternitySecretEdges: (childId: string, isSecretForBioFather: boolean, isSecretForPartner: boolean) => {
    sendMarasAction('setPaternitySecretEdges', { childId, isSecretForBioFather, isSecretForPartner })
    set((state) => {
      const isSecret = isSecretForBioFather || isSecretForPartner
      const newChildren = new Map(state.children)
      const existing = newChildren.get(childId)
      if (existing) {
        newChildren.set(childId, {
          ...existing,
          isSecret,
          isSecretForBioFather,
          isSecretForPartner,
        })
      }
      return { children: newChildren }
    })
  },

  updateSetting: <K extends keyof MarasSettings>(key: K, value: MarasSettings[K]) => {
    set((state) => {
      const newSettings = { ...state.settings, [key]: value }
      if (key === 'simulateNoSkyrimNet') {
        if (value) {
          newSettings.hasSkyrimNet = false
        } else if (!isGameBuild) {
          newSettings.hasSkyrimNet = true
        }
      }
      return { settings: newSettings }
    })

    const keyStr = key as string
    const existingTimer = updateSettingDebounceTimers.get(keyStr)
    if (existingTimer) {
      clearTimeout(existingTimer)
      updateSettingDebounceTimers.delete(keyStr)
    }

    // Debounce continuous numeric settings (sliders and numeric inputs) by 300ms
    if (typeof value === 'number') {
      pendingSettingUpdates.set(keyStr, value)
      const timer = setTimeout(() => {
        updateSettingDebounceTimers.delete(keyStr)
        pendingSettingUpdates.delete(keyStr)
        sendMarasAction('updateSetting', { key, value })
      }, 300)
      updateSettingDebounceTimers.set(keyStr, timer)
    } else {
      pendingSettingUpdates.delete(keyStr)
      sendMarasAction('updateSetting', { key, value })
    }
  },

  dumpData: () => {
    set({ isDumpingData: true })
    sendMarasAction('dumpData')
    if (!isGameBuild) {
      setTimeout(() => {
        const timeStr = new Date().toLocaleString()
        set({
          isDumpingData: false,
          lastDumpResult: {
            success: true,
            fileName: `maras_dump_${new Date().toISOString().replace(/[:.]/g, '-')}.json`,
            filePath: 'Data/SKSE/Plugins/MARAS/dumps/',
            absolutePath: 'E:/Skyrim/Data/SKSE/Plugins/MARAS/dumps/maras_dump_preview.json',
            timestamp: timeStr,
          },
        })
      }, 500)
    }
  },

  setIsDumpingData: (isDumping: boolean) => {
    set({ isDumpingData: isDumping })
  },

  setLastDumpResult: (result: DataDumpResult | null) => {
    set({ lastDumpResult: result })
  },

  dumpNPCIndex: () => {
    set({ isDumpingNPCIndex: true })
    sendMarasAction('dumpNPCIndex')
    if (!isGameBuild) {
      setTimeout(() => {
        const timeStr = new Date().toLocaleString()
        set({
          isDumpingNPCIndex: false,
          lastNPCIndexDumpResult: {
            success: true,
            fileName: `maras_npc_index_${new Date().toISOString().replace(/[:.]/g, '-')}.json`,
            filePath: 'Data/SKSE/Plugins/MARAS/dumps/',
            absolutePath: 'E:/Skyrim/Data/SKSE/Plugins/MARAS/dumps/maras_npc_index_preview.json',
            totalNPCs: 1420,
            timestamp: timeStr,
          },
        })
      }, 500)
    }
  },

  setIsDumpingNPCIndex: (isDumping: boolean) => {
    set({ isDumpingNPCIndex: isDumping })
  },

  setLastNPCIndexDumpResult: (result: DataDumpResult | null) => {
    set({ lastNPCIndexDumpResult: result })
  },

  closeUi: () => {
    flushAllPendingDebouncedActions()
    sendMarasAction('closeUi')
    if (typeof window !== 'undefined' && window.closeMARAS) {
      window.closeMARAS()
    } else {
      window.log?.('closeMARAS called (standalone mode)')
    }
    set({ isOpen: false })
  },

  logTestSignal: () => {
    const { statusMsg } = get()
    window.log?.(`Test signal dispatched from MARAS UI Store [Status: ${statusMsg}]`)
  },
}))
