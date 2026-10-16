import { useMarasStore, flushAllPendingDebouncedActions } from './useMarasStore'
import {
  NPCProfile,
  RelationshipPair,
  AffectionData,
  RelationshipStatus,
  SocialClass,
  SkillType,
  Temperament,
  AffairStatus,
} from '../types'

declare global {
  interface Window {
    marasAction?: (jsonStr: string) => void
    receiveMARASOverview?: (data: any) => void
    receiveMARASBuffs?: (data: any) => void
    receiveNPCList?: (list: any[] | string) => void
    receiveNPCDetail?: (detail: any) => void
    onAffectionChanged?: (data: { formId: string; value: number } | string) => void
    onStatusChanged?: (data: { actorA: string; actorB: string; status: number; isOpen?: boolean } | string) => void
    onHierarchyChanged?: (data?: { ranks?: [string, string, string] } | [string, string, string] | string) => void
    onChildUpdated?: (child: any) => void
    onChildRemoved?: (data: { childId: string } | string) => void
    onChildPromoted?: (oldIdOrData: { oldId: string; newId: string } | string, newId?: string) => void
    openMARAS?: () => void
    showMARAS?: () => void
    refocusView?: (unused?: string) => void
    receiveNPCSearchResults?: (data: { query: string; side: string; results: any[] } | string) => void
    receiveAllSkyrimNPCs?: (data: { npcs: any[] } | string) => void
    receiveMatchmakingResults?: (resultsOrPayload: any[] | { results: any[]; isGlobal?: boolean } | string, isGlobal?: boolean) => void
    receiveMatchmakingSettings?: (settings: any) => void
    receiveGeneratedMemories?: (data: { partnerFormId: string; memories: any[] } | string) => void
    onDataDumpCompleted?: (data: { success: boolean; fileName?: string; filePath?: string; absolutePath?: string; timestamp?: string; error?: string } | string) => void
    onNPCIndexDumpCompleted?: (data: { success: boolean; fileName?: string; filePath?: string; absolutePath?: string; timestamp?: string; totalNPCs?: number; error?: string } | string) => void
    onActionRejected?: (data: { action: string; actorA?: string; actorB?: string; status?: number; reason?: string } | string) => void
  }
}

function parsePayload<T = any>(raw: T | string): T {
  if (typeof raw === 'string') {
    const trimmed = raw.trim()
    if ((trimmed.startsWith('{') && trimmed.endsWith('}')) || (trimmed.startsWith('[') && trimmed.endsWith(']'))) {
      try {
        return JSON.parse(trimmed) as T
      } catch (e) {
        console.error('[PrismaBridge] Failed to parse JSON payload:', e)
      }
    }
  }
  return raw as T
}

export function sendMarasAction(action: string, payload: Record<string, any> = {}) {
  const jsonStr = JSON.stringify({ action, payload })
  if (typeof window !== 'undefined' && window.marasAction) {
    window.marasAction(jsonStr)
  } else {
    console.log(`[Standalone Mock Action] ${action}:`, payload)
  }
}

export function spawnChild(childId: string) {
  sendMarasAction('spawnChild', { childId })
}

export function initializePrismaBridge() {
  if (typeof window === 'undefined') return

  // 1. Receive Overview Snapshot from C++
  window.receiveMARASOverview = (raw: any) => {
    const data = parsePayload(raw)
    if (!data || typeof data !== 'object') return
    console.log('[PrismaBridge] receiveMARASOverview payload:', data)
    const store = useMarasStore.getState()
    store.setIsOpen(true)

    // Reconcile real child names from children array into npcProfiles if npcProfiles has placeholder names
    if (data.children && data.npcProfiles) {
      const childNameMap = new Map<string, string>()
      data.children.forEach((c: any) => {
        if (c.childId && c.name && !c.name.startsWith('NPC_')) {
          childNameMap.set(c.childId, c.name)
        }
      })
      data.npcProfiles.forEach((prof: any) => {
        if (childNameMap.has(prof.formId) && (!prof.name || prof.name.startsWith('NPC_'))) {
          prof.name = childNameMap.get(prof.formId)
        }
      })
    }

    store.setLiveGameData({
      currentGameDay: data.currentGameDay,
      npcProfiles: data.npcProfiles,
      relationships: data.relationships,
      children: data.children,
      cheatingRecords: data.cheatingRecords,
      affectionData: data.affectionData,
      hierarchyRanks: data.hierarchyRanks,
      playerHomes: data.playerHomes,
      settings: data.settings,
    })
    if (data.children) {
      useMarasStore.setState({ evaluatingChildId: null })
    }
  }

  // 1a. Receive Household Buffs from C++
  window.receiveMARASBuffs = (raw: any) => {
    const data = parsePayload(raw)
    console.log('[PrismaBridge] receiveMARASBuffs payload:', data)
    useMarasStore.getState().setHouseholdBuffs(data)
  }

  // 1b. Direct View Show/Open callbacks from SKSE / Prisma UI
  window.openMARAS = () => {
    console.log('[PrismaBridge] openMARAS called')
    useMarasStore.getState().setIsOpen(true)
    window.refocusView?.()
  }
  window.showMARAS = () => {
    console.log('[PrismaBridge] showMARAS called')
    useMarasStore.getState().setIsOpen(true)
    window.refocusView?.()
  }

  // Auto-acquire OS/game input focus when clicking or interacting with inputs
  document.addEventListener('mousedown', () => {
    if (!document.hasFocus()) {
      window.refocusView?.()
    }
  })

  document.addEventListener('focusin', () => {
    if (!document.hasFocus()) {
      window.refocusView?.()
    }
  })

  // Flush any pending debounced actions (settings/sliders) before window unload or close
  window.addEventListener('beforeunload', () => {
    flushAllPendingDebouncedActions()
  })
  window.addEventListener('pagehide', () => {
    flushAllPendingDebouncedActions()
  })

  // 2. Receive NPC List Lazy Load Response
  window.receiveNPCList = (rawList: any[] | string) => {
    const list = parsePayload<any[]>(rawList)
    if (!Array.isArray(list)) return
    console.log('[PrismaBridge] receiveNPCList:', list)
    const store = useMarasStore.getState()

    const profiles: NPCProfile[] = list.map((item) => {
      const existing = store.npcProfiles.get(item.formId)
      return {
        formId: item.formId,
        name: item.name,
        race: item.race || 'Nord',
        socialClass: existing?.socialClass ?? item.socialClass ?? SocialClass.Middle,
        skillType: existing?.skillType ?? item.skillType ?? SkillType.Warrior,
        temperament: existing?.temperament ?? item.temperament ?? Temperament.Proud,
        currentHome: item.currentHome ?? existing?.currentHome,
        originalHome: item.originalHome ?? existing?.originalHome,
        isChild: item.isChild ?? existing?.isChild,
        stage: item.stage ?? existing?.stage,
        isEphemeral: item.isEphemeral ?? existing?.isEphemeral,
        hasRefID: item.hasRefID ?? existing?.hasRefID,
        trainingLocation: item.trainingLocation ?? existing?.trainingLocation,
        critterBanned: existing?.critterBanned ?? item.critterBanned,
        petBanned: existing?.petBanned ?? item.petBanned,
        currentCritter: item.currentCritter !== undefined ? item.currentCritter : existing?.currentCritter,
        currentPet: item.currentPet !== undefined ? item.currentPet : existing?.currentPet,
        pluginName: item.pluginName || existing?.pluginName,
        gender: item.gender ?? existing?.gender ?? 'male',
      }
    })

    store.setLiveGameData({ npcProfiles: profiles })
  }

  // 3. Receive NPC Detail Lazy Load Response
  window.receiveNPCDetail = (rawDetail: any) => {
    const detail = parsePayload(rawDetail)
    console.log('[PrismaBridge] receiveNPCDetail:', detail)
    const store = useMarasStore.getState()
    if (detail && detail.formId) {
      const existing = store.npcProfiles.get(detail.formId)
      let resolvedName = detail.name
      if (!resolvedName || resolvedName.startsWith('NPC_')) {
        if (existing?.name && !existing.name.startsWith('NPC_')) {
          resolvedName = existing.name
        } else {
          const childMatch = store.children.get(detail.formId)
          if (childMatch && childMatch.name && !childMatch.name.startsWith('NPC_')) {
            resolvedName = childMatch.name
          }
        }
      }

      const isChild = Boolean(existing?.isChild ?? detail.isChild ?? store.children.has(detail.formId))
      store.updateNPCProfile(
        detail.formId,
        {
          name: resolvedName,
          race: detail.race,
          pluginName: detail.pluginName,
          socialClass: detail.socialClass ?? SocialClass.Middle,
          skillType: detail.skillType ?? SkillType.Warrior,
          temperament: detail.temperament ?? Temperament.Proud,
          currentHome: detail.currentHome,
          originalHome: detail.originalHome,
          stayAtHome: detail.stayAtHome,
          isChild,
          stage: detail.stage,
          isEphemeral: detail.isEphemeral,
          hasRefID: detail.hasRefID,
          trainingLocation: detail.trainingLocation,
          critterBanned: detail.critterBanned,
          petBanned: detail.petBanned,
          currentCritter: detail.currentCritter,
          currentPet: detail.currentPet,
          gender: detail.gender ?? existing?.gender ?? 'male',
        },
        true // skipBridge = true (incoming from C++)
      )

      if (detail.affection && typeof detail.affection.permanent === 'number') {
        store.updateAffection(detail.formId, detail.affection.permanent, true) // skipBridge = true (incoming from C++)
      }
    }
  }

  // 4. Real-time Event Sink: Affection Changed
  window.onAffectionChanged = (raw) => {
    const data = parsePayload<{ formId: string; value: number }>(raw)
    if (!data || !data.formId) return
    console.log(`[PrismaBridge] onAffectionChanged for ${data.formId}: ${data.value}`)
    useMarasStore.getState().updateAffection(data.formId, data.value, true) // skipBridge = true (incoming from C++)
  }

  // 5. Real-time Event Sink: Relationship Status Changed
  window.onStatusChanged = (raw) => {
    const data = parsePayload<{ actorA: string; actorB: string; status: number; isOpen?: boolean }>(raw)
    if (!data || !data.actorA || !data.actorB) return
    console.log(`[PrismaBridge] onStatusChanged: ${data.actorA} <-> ${data.actorB} = ${data.status} (isOpen: ${data.isOpen})`)
    useMarasStore.getState().handleRealtimeStatusChanged(data.actorA, data.actorB, data.status as RelationshipStatus, data.isOpen)
  }

  // 6. Real-time Event Sink: Hierarchy Changed
  window.onHierarchyChanged = (raw) => {
    const data = raw !== undefined ? parsePayload<{ ranks?: [string, string, string] } | [string, string, string]>(raw) : undefined
    console.log('[PrismaBridge] onHierarchyChanged:', data)
    const ranks = Array.isArray(data) ? data : data?.ranks
    if (ranks) {
      useMarasStore.setState({ hierarchyRanks: ranks })
    } else {
      sendMarasAction('fetchOverview')
    }
  }

  // 6b. Real-time Event Sink: Child Updated
  window.onChildUpdated = (rawChild: any) => {
    const child = parsePayload(rawChild)
    console.log('[PrismaBridge] onChildUpdated:', child)
    if (child && child.childId) {
      useMarasStore.getState().updateChild(child.childId, child, true)
    }
  }

  // 6c. Real-time Event Sink: Child Removed
  window.onChildRemoved = (rawData: { childId: string } | string) => {
    const data = parsePayload<{ childId: string } | string>(rawData)
    console.log('[PrismaBridge] onChildRemoved:', data)
    const childId = typeof data === 'string' ? data : data?.childId
    if (childId) {
      useMarasStore.getState().removeChild(childId, true)
    }
  }

  // 6d. Real-time Event Sink: Child Promoted (Synthetic -> Physical Actor)
  window.onChildPromoted = (oldIdOrData: { oldId: string; newId: string } | string, newId?: string) => {
    const parsed = parsePayload<{ oldId: string; newId: string } | string>(oldIdOrData)
    const resolvedOldId = typeof parsed === 'object' && parsed !== null ? parsed.oldId : parsed
    const resolvedNewId = typeof parsed === 'object' && parsed !== null ? parsed.newId : newId
    if (resolvedOldId && resolvedNewId) {
      console.log(`[PrismaBridge] onChildPromoted: ${resolvedOldId} -> ${resolvedNewId}`)
      useMarasStore.getState().handleChildPromoted(resolvedOldId, resolvedNewId)
    }
  }

  // 7. Receive NPC Search Results (Matchmaking)
  window.receiveNPCSearchResults = (raw) => {
    const data = parsePayload<{ query: string; side: string; results: any[] }>(raw)
    if (!data) return
    console.log('[PrismaBridge] receiveNPCSearchResults:', data)
    const side = (data.side || 'A').toLowerCase() as 'a' | 'b' | 'modal'
    useMarasStore.getState().setSearchResults(side, data.results || [])
  }

  // 8. Receive All Skyrim NPCs
  window.receiveAllSkyrimNPCs = (raw) => {
    const data = parsePayload<{ npcs: any[] }>(raw)
    if (!data) return
    console.log('[PrismaBridge] receiveAllSkyrimNPCs:', data)
    useMarasStore.getState().setAllSkyrimNpcs(data.npcs || [])
  }

  // 9. Receive Data Dump Completed Notification
  window.onDataDumpCompleted = (raw) => {
    const data = parsePayload<{ success: boolean; fileName?: string; filePath?: string; absolutePath?: string; timestamp?: string; error?: string }>(raw)
    if (!data) return
    console.log('[PrismaBridge] onDataDumpCompleted:', data)
    const store = useMarasStore.getState()
    store.setIsDumpingData(false)
    store.setLastDumpResult({
      success: !!data.success,
      fileName: data.fileName,
      filePath: data.filePath,
      absolutePath: data.absolutePath,
      timestamp: data.timestamp || new Date().toLocaleString(),
      error: data.error,
    })
  }

  // 10. Receive NPC Index Dump Completed Notification
  window.onNPCIndexDumpCompleted = (raw) => {
    const data = parsePayload<{ success: boolean; fileName?: string; filePath?: string; absolutePath?: string; timestamp?: string; totalNPCs?: number; error?: string }>(raw)
    if (!data) return
    console.log('[PrismaBridge] onNPCIndexDumpCompleted:', data)
    const store = useMarasStore.getState()
    store.setIsDumpingNPCIndex(false)
    store.setLastNPCIndexDumpResult({
      success: !!data.success,
      fileName: data.fileName,
      filePath: data.filePath,
      absolutePath: data.absolutePath,
      totalNPCs: data.totalNPCs,
      timestamp: data.timestamp || new Date().toLocaleString(),
      error: data.error,
    })
  }

  // 11. Receive Action Rejection Notification (e.g. invalid relationship mutation)
  window.onActionRejected = (raw) => {
    const data = parsePayload<{ action: string; actorA?: string; actorB?: string; status?: number; reason?: string }>(raw)
    if (!data) return
    console.warn('[PrismaBridge] onActionRejected:', data)
    const store = useMarasStore.getState()
    const reason = data.reason || 'Operation could not proceed due to relationship rules.'
    store.setStatusMsg(`Action Blocked: ${reason}`)
  }

  // 12. Receive Matchmaking Results from SkyrimNet LLM
  window.receiveMatchmakingResults = (rawResultsOrPayload: any[] | { results: any[]; isGlobal?: boolean } | string, isGlobal?: boolean) => {
    const parsed = parsePayload<any[] | { results: any[]; isGlobal?: boolean }>(rawResultsOrPayload)
    const results = Array.isArray(parsed) ? parsed : (parsed?.results || [])
    const resolvedIsGlobal = typeof isGlobal === 'boolean' ? isGlobal : (!Array.isArray(parsed) ? parsed?.isGlobal : undefined)
    console.log('[PrismaBridge] receiveMatchmakingResults:', results, resolvedIsGlobal)
    const store = useMarasStore.getState()
    if (typeof resolvedIsGlobal === 'boolean') {
      store.setMatchmakingScanType(resolvedIsGlobal ? 'global' : 'target')
    }
    const formatted = (results || []).map((item) => ({
      formId: item.formId || item.actorBFormId,
      name: item.name || item.actorBName,
      actorAFormId: item.actorAFormId,
      actorAName: item.actorAName,
      suggestedStatus: item.suggestedStatus ?? RelationshipStatus.Dating,
      action: item.action || 'new',
      targetStatus: item.targetStatus || 'dating',
      selected: item.selected ?? true,
      compatibilityScore: item.compatibilityScore ?? 85,
      isCheating: Boolean(item.isCheating ?? item.cheating ?? false),
      reasoning: item.reasoning || item.memory || '',
      memory: item.memory || item.reasoning || '',
      synergyTags: item.synergyTags || ['#AIContext', '#SkyrimLore'],
    }))
    store.setAiSuggestions(formatted)
  }

  // 13. Receive Matchmaking Settings
  window.receiveMatchmakingSettings = (rawSettings: any) => {
    const settings = parsePayload(rawSettings)
    console.log('[PrismaBridge] receiveMatchmakingSettings:', settings)
    const store = useMarasStore.getState()
    store.setMatchmakingSettings(settings)
  }

  // 14. Receive Generated Dual Memories from SkyrimNet LLM
  window.receiveGeneratedMemories = (raw: { partnerFormId: string; memories: any[] } | string) => {
    const data = parsePayload<{ partnerFormId: string; memories: any[] }>(raw)
    if (!data) return
    console.log('[PrismaBridge] receiveGeneratedMemories:', data)
    const store = useMarasStore.getState()
    store.receiveGeneratedMemories(data)
  }

  console.log('[PrismaBridge] Window callbacks initialized successfully')
}

export function fetchHouseholdBuffs() {
  sendMarasAction('fetchHouseholdBuffs')
}

