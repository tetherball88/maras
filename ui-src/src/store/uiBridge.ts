import { useMarasStore, flushAllPendingDebouncedActions } from './useMarasStore'
import { useModalStore } from './useModalStore'
import { useLocaleStore } from './useLocaleStore'
import { t } from '../i18n'
import {
  NPCProfile,
  RelationshipPair,
  AffectionData,
  RelationshipStatus,
  SocialClass,
  SkillType,
  Temperament,
  AffairStatus,
  ModalPayload,
  LocalePayload,
} from '../types'

declare global {
  interface Window {
    marasAction?: (jsonStr: string) => void
    receiveMARASLocale?: (data: LocalePayload | string) => void
    receiveMARASOverview?: (data: any) => void
    receiveMARASBuffs?: (data: any) => void
    receiveNPCList?: (list: any[] | string) => void
    receiveNPCDetail?: (detail: any) => void
    onAffectionChanged?: (data: { formId: string; value: number } | string) => void
    onStatusChanged?: (
      data:
        | {
            actorA: string
            actorB: string
            status: number
            isOpen?: boolean
            actorAProfile?: NPCProfile
            actorBProfile?: NPCProfile
          }
        | string
    ) => void
    onHierarchyChanged?: (data?: { ranks?: [string, string, string] } | [string, string, string] | string) => void
    onChildUpdated?: (child: any) => void
    onChildRemoved?: (data: { childId: string } | string) => void
    onChildPromoted?: (oldIdOrData: { oldId: string; newId: string } | string, newId?: string) => void
    openMARAS?: () => void
    showMARAS?: () => void
    closeMARASView?: () => void
    onGamePausedChanged?: (data: { paused: boolean } | string) => void
    showMARASModal?: (modalData: ModalPayload | string) => void
    clearMARASModal?: () => void
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
        console.error('[UIBridge] Failed to parse JSON payload:', e)
      }
    }
  }
  return raw as T
}

async function fetchDevLocale(langCode: string): Promise<LocalePayload | null> {
  if (typeof window === 'undefined' || window.marasAction) return null
  try {
    const res = await fetch(`/__dev/locale/${encodeURIComponent(langCode || 'auto')}`)
    if (!res.ok) return null
    const data = (await res.json()) as LocalePayload
    if (data && typeof data === 'object') {
      useLocaleStore.getState().applyLocalePayload(data)
      return data
    }
  } catch {
    // Ignore when static preview without Vite dev middleware
  }
  return null
}

export function sendMarasAction(action: string, payload: Record<string, any> = {}) {
  const jsonStr = JSON.stringify({ action, payload })
  if (typeof window !== 'undefined' && window.marasAction) {
    window.marasAction(jsonStr)
  } else {
    console.log(`[Standalone Mock Action] ${action}:`, payload)
    if (action === 'updateSetting' && payload?.key === 'language' && typeof payload.value === 'string') {
      void fetchDevLocale(payload.value)
    }
  }
}

export function reloadLocalization() {
  if (typeof window !== 'undefined' && window.marasAction) {
    sendMarasAction('reloadLocalization')
    const resolved = useLocaleStore.getState().resolvedLanguage || 'en'
    useMarasStore.getState().setStatusMsg(t('bridge.localizationReloaded', { lang: resolved }))
  } else {
    const selected = useLocaleStore.getState().selectedLanguage || 'auto'
    void fetchDevLocale(selected).then((loaded) => {
      const resolved = loaded?.resolvedLanguage || useLocaleStore.getState().resolvedLanguage || 'en'
      useMarasStore.getState().setStatusMsg(t('bridge.localizationReloaded', { lang: resolved }))
    })
  }
}

export function spawnChild(childId: string) {
  sendMarasAction('spawnChild', { childId })
}

export function sendChildToTraining(childId: string, trainingClassIndex: number) {
  sendMarasAction('sendChildToTraining', { childId, trainingClassIndex })
}

export function completeChildTraining(childId: string) {
  sendMarasAction('completeChildTraining', { childId })
}

export function initializeUIBridge() {
  if (typeof window === 'undefined') return

  // 0. Receive Localization Dictionary & Language Metadata from C++
  window.receiveMARASLocale = (raw: LocalePayload | string) => {
    const data = parsePayload<LocalePayload>(raw)
    if (!data || typeof data !== 'object') return
    console.log('[UIBridge] receiveMARASLocale:', {
      selected: data.selectedLanguage,
      resolved: data.resolvedLanguage,
      available: data.availableLanguages?.length ?? 0,
    })
    useLocaleStore.getState().applyLocalePayload(data)
  }

  if (!window.marasAction) {
    void fetchDevLocale('auto')
  }

  // 1. Receive Overview Snapshot from C++
  window.receiveMARASOverview = (raw: any) => {
    const data = parsePayload(raw)
    if (!data || typeof data !== 'object') return
    console.log('[UIBridge] receiveMARASOverview payload:', data)
    const store = useMarasStore.getState()

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
      isGamePaused: data.isGamePaused,
    })
    if (data.children) {
      useMarasStore.setState({ evaluatingChildId: null })
    }
  }

  // 1a. Receive Household Buffs from C++
  window.receiveMARASBuffs = (raw: any) => {
    const data = parsePayload(raw)
    console.log('[UIBridge] receiveMARASBuffs payload:', data)
    useMarasStore.getState().setHouseholdBuffs(data)
  }

  // 1b. Direct View Show/Open/Close callbacks from SKSE / UI Host
  window.openMARAS = () => {
    console.log('[UIBridge] openMARAS called')
    useMarasStore.getState().setIsOpen(true)
  }
  window.showMARAS = () => {
    console.log('[UIBridge] showMARAS called')
    useMarasStore.getState().setIsOpen(true)
  }
  window.closeMARASView = () => {
    console.log('[UIBridge] closeMARASView called')
    flushAllPendingDebouncedActions()
    useMarasStore.getState().setIsOpen(false)
  }
  window.onGamePausedChanged = (raw: { paused: boolean } | string) => {
    const data = parsePayload<{ paused: boolean }>(raw)
    console.log('[UIBridge] onGamePausedChanged:', data)
    if (data && typeof data.paused === 'boolean') {
      useMarasStore.getState().setGamePaused(data.paused, true)
    }
  }

  // 1c. Standalone/Blocking UI Modal callbacks from C++
  window.showMARASModal = (rawModal: ModalPayload | string) => {
    const modal = parsePayload<ModalPayload>(rawModal)
    if (!modal || typeof modal !== 'object' || !modal.modalId) {
      console.error('[UIBridge] Invalid showMARASModal payload:', rawModal)
      return
    }
    console.log('[UIBridge] showMARASModal:', modal)
    useModalStore.getState().showModal(modal)
  }
  window.clearMARASModal = () => {
    console.log('[UIBridge] clearMARASModal called')
    useModalStore.getState().clearModal()
  }

  // Flush any pending debounced actions (settings/sliders) before window unload, hide, or close
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden') {
      flushAllPendingDebouncedActions()
    }
  })
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
    console.log('[UIBridge] receiveNPCList:', list)
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
        trainingClassIndex: item.trainingClassIndex ?? existing?.trainingClassIndex,
        trainingClassName: item.trainingClassName ?? existing?.trainingClassName,
        trainingStartGameDay: item.trainingStartGameDay ?? existing?.trainingStartGameDay,
        critterBanned: existing?.critterBanned ?? item.critterBanned,
        petBanned: existing?.petBanned ?? item.petBanned,
        currentCritter: item.currentCritter !== undefined ? item.currentCritter : existing?.currentCritter,
        currentPet: item.currentPet !== undefined ? item.currentPet : existing?.currentPet,
        pluginName: item.pluginName || existing?.pluginName,
        gender: item.gender ?? existing?.gender ?? 'male',
        pendingIntent: item.pendingIntent ?? existing?.pendingIntent,
      }
    })

    store.setLiveGameData({ npcProfiles: profiles })
  }

  // 3. Receive NPC Detail Lazy Load Response
  window.receiveNPCDetail = (rawDetail: any) => {
    const detail = parsePayload(rawDetail)
    console.log('[UIBridge] receiveNPCDetail:', detail)
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
          trainingClassIndex: detail.trainingClassIndex,
          trainingClassName: detail.trainingClassName,
          trainingStartGameDay: detail.trainingStartGameDay,
          critterBanned: detail.critterBanned,
          petBanned: detail.petBanned,
          currentCritter: detail.currentCritter,
          currentPet: detail.currentPet,
          gender: detail.gender ?? existing?.gender ?? 'male',
          pendingIntent: detail.pendingIntent ?? existing?.pendingIntent,
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
    console.log(`[UIBridge] onAffectionChanged for ${data.formId}: ${data.value}`)
    useMarasStore.getState().updateAffection(data.formId, data.value, true) // skipBridge = true (incoming from C++)
  }

  // 5. Real-time Event Sink: Relationship Status Changed
  window.onStatusChanged = (raw) => {
    const data = parsePayload<{
      actorA: string
      actorB: string
      status: number
      isOpen?: boolean
      actorAProfile?: NPCProfile
      actorBProfile?: NPCProfile
    }>(raw)
    if (!data || !data.actorA || !data.actorB) return
    console.log(`[UIBridge] onStatusChanged: ${data.actorA} <-> ${data.actorB} = ${data.status} (isOpen: ${data.isOpen})`)
    useMarasStore
      .getState()
      .handleRealtimeStatusChanged(
        data.actorA,
        data.actorB,
        data.status as RelationshipStatus,
        data.isOpen,
        data.actorAProfile,
        data.actorBProfile,
      )
  }

  // 6. Real-time Event Sink: Hierarchy Changed
  window.onHierarchyChanged = (raw) => {
    const data = raw !== undefined ? parsePayload<{ ranks?: [string, string, string] } | [string, string, string]>(raw) : undefined
    console.log('[UIBridge] onHierarchyChanged:', data)
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
    console.log('[UIBridge] onChildUpdated:', child)
    if (child && child.childId) {
      useMarasStore.getState().updateChild(child.childId, child, true)
    }
  }

  // 6c. Real-time Event Sink: Child Removed
  window.onChildRemoved = (rawData: { childId: string } | string) => {
    const data = parsePayload<{ childId: string } | string>(rawData)
    console.log('[UIBridge] onChildRemoved:', data)
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
      console.log(`[UIBridge] onChildPromoted: ${resolvedOldId} -> ${resolvedNewId}`)
      useMarasStore.getState().handleChildPromoted(resolvedOldId, resolvedNewId)
    }
  }

  // 7. Receive NPC Search Results (Matchmaking)
  window.receiveNPCSearchResults = (raw) => {
    const data = parsePayload<{ query: string; side: string; results: any[] }>(raw)
    if (!data) return
    console.log('[UIBridge] receiveNPCSearchResults:', data)
    const side = (data.side || 'A').toLowerCase() as 'a' | 'b' | 'modal'
    useMarasStore.getState().setSearchResults(side, data.results || [])
  }

  // 8. Receive All Skyrim NPCs
  window.receiveAllSkyrimNPCs = (raw) => {
    const data = parsePayload<{ npcs: any[] }>(raw)
    if (!data) return
    console.log('[UIBridge] receiveAllSkyrimNPCs:', data)
    useMarasStore.getState().setAllSkyrimNpcs(data.npcs || [])
  }

  // 9. Receive Data Dump Completed Notification
  window.onDataDumpCompleted = (raw) => {
    const data = parsePayload<{ success: boolean; fileName?: string; filePath?: string; absolutePath?: string; timestamp?: string; error?: string }>(raw)
    if (!data) return
    console.log('[UIBridge] onDataDumpCompleted:', data)
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
    console.log('[UIBridge] onNPCIndexDumpCompleted:', data)
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
    console.warn('[UIBridge] onActionRejected:', data)
    const store = useMarasStore.getState()
    const reason = data.reason || t('bridge.actionBlockedDefault')
    store.setStatusMsg(t('bridge.actionBlockedStatus', { reason }))
  }

  // 12. Receive Matchmaking Results from SkyrimNet LLM
  window.receiveMatchmakingResults = (rawResultsOrPayload: any[] | { results: any[]; isGlobal?: boolean } | string, isGlobal?: boolean) => {
    const parsed = parsePayload<any[] | { results: any[]; isGlobal?: boolean }>(rawResultsOrPayload)
    const results = Array.isArray(parsed) ? parsed : (parsed?.results || [])
    const resolvedIsGlobal = typeof isGlobal === 'boolean' ? isGlobal : (!Array.isArray(parsed) ? parsed?.isGlobal : undefined)
    console.log('[UIBridge] receiveMatchmakingResults:', results, resolvedIsGlobal)
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
    console.log('[UIBridge] receiveMatchmakingSettings:', settings)
    const store = useMarasStore.getState()
    store.setMatchmakingSettings(settings)
  }

  // 14. Receive Generated Dual Memories from SkyrimNet LLM
  window.receiveGeneratedMemories = (raw: { partnerFormId: string; memories: any[] } | string) => {
    const data = parsePayload<{ partnerFormId: string; memories: any[] }>(raw)
    if (!data) return
    console.log('[UIBridge] receiveGeneratedMemories:', data)
    const store = useMarasStore.getState()
    store.receiveGeneratedMemories(data)
  }

  console.log('[UIBridge] Window callbacks initialized successfully')
}

export function fetchHouseholdBuffs() {
  sendMarasAction('fetchHouseholdBuffs')
}

