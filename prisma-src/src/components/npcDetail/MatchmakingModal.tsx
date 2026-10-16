import React, { useState, useEffect } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus, NPCProfile, CompactNPC } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { SearchInput } from '../shared/SearchInput'
import { SelectInput } from '../shared/SelectInput'
import { VirtualList } from '../shared/VirtualList'
import { X, Sparkles, User, Check, Bot, Search, HeartHandshake, RefreshCw, BookOpen } from 'lucide-react'
import { useAvailableStatuses, getAvailableStatuses } from '../../utils/statusAvailability'

export const MatchmakingModal: React.FC = () => {
  const isOpen = useMarasStore((s) => s.matchmakingModalOpen)
  const setOpen = useMarasStore((s) => s.setMatchmakingModalOpen)
  const targetNpcId = useMarasStore((s) => s.matchmakingTargetNpcId)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const allSkyrimNpcs = useMarasStore((s) => s.allSkyrimNpcs)
  const isLoadingAllNpcs = useMarasStore((s) => s.isLoadingAllNpcs)
  const fetchAllSkyrimNpcs = useMarasStore((s) => s.fetchAllSkyrimNpcs)
  const settings = useMarasStore((s) => s.settings)

  const assignRelationshipStatus = useMarasStore((s) => s.assignRelationshipStatus)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)

  const aiSuggestions = useMarasStore((s) => s.aiSuggestions)
  const isGeneratingAi = useMarasStore((s) => s.isGeneratingAiSuggestions)
  const generateAiSuggestions = useMarasStore((s) => s.generateAiSuggestions)
  const generateMatchMemories = useMarasStore((s) => s.generateMatchMemories)
  const toggleAiSuggestionSelected = useMarasStore((s) => s.toggleAiSuggestionSelected)
  const updateAiSuggestionStatus = useMarasStore((s) => s.updateAiSuggestionStatus)
  const applyAiSuggestions = useMarasStore((s) => s.applyAiSuggestions)

  const [activeTab, setActiveTab] = useState<'manual' | 'ai'>('manual')
  const [search, setSearch] = useState('')
  const [selectedPartnerId, setSelectedPartnerId] = useState<string | null>(null)
  const [selectedStatus, setSelectedStatus] = useState<RelationshipStatus>(RelationshipStatus.Dating)

  // Ensure all Skyrim NPCs are fetched on modal open if not already loaded
  useEffect(() => {
    if (isOpen && allSkyrimNpcs.length === 0) {
      fetchAllSkyrimNpcs()
    }
  }, [isOpen, allSkyrimNpcs.length, fetchAllSkyrimNpcs])

  // Reset local state when modal closes
  useEffect(() => {
    if (!isOpen) {
      setSelectedPartnerId(null)
      setSearch('')
    }
  }, [isOpen])

  // Call status availability hook unconditionally at top level
  const manualStatusOptions = useAvailableStatuses(
    isOpen ? targetNpcId : null,
    isOpen ? selectedPartnerId : null
  )
  const currentSelectedOption = manualStatusOptions.find((o) => o.value === selectedStatus)
  const canConfirmManual = selectedPartnerId ? (currentSelectedOption?.enabled ?? false) : false

  // Automatically select first available status if current selection is invalid
  useEffect(() => {
    if (isOpen && selectedPartnerId && manualStatusOptions.length > 0) {
      const currentOpt = manualStatusOptions.find((o) => o.value === selectedStatus)
      if (!currentOpt || !currentOpt.enabled) {
        const firstAvailable = manualStatusOptions.find((o) => o.enabled)
        if (firstAvailable) {
          setSelectedStatus(firstAvailable.value)
        }
      }
    }
  }, [isOpen, selectedPartnerId, manualStatusOptions, selectedStatus])

  if (!isOpen || !targetNpcId) return null

  const isSkyrimNetEnabled = Boolean(settings.hasSkyrimNet)
  const playerProfile = npcProfiles.get(PLAYER_FORM_ID)
  const playerName = playerProfile?.name || 'Prisoner (Player)'
  const playerRace = playerProfile?.race || 'Nord'
  const playerGender = playerProfile?.gender || 'female'
  const targetName = targetNpcId === PLAYER_FORM_ID ? playerName : npcProfiles.get(targetNpcId)?.name || targetNpcId

  // Source list: Player (if target is not player) + all non-target, non-child Skyrim NPCs
  const candidateNpcs: Array<CompactNPC | { formId: string; name: string; race: string; gender?: CompactNPC['gender']; isChild?: boolean; pluginName?: string }> = [
    ...(targetNpcId !== PLAYER_FORM_ID ? [{ formId: PLAYER_FORM_ID, name: playerName, race: playerRace, gender: playerGender, pluginName: 'Skyrim.esm' }] : []),
    ...allSkyrimNpcs.filter((n) => n.formId !== targetNpcId && !n.isChild),
  ]

  const searchLower = search.trim().toLowerCase()
  const displayNpcs = searchLower.length === 0
    ? candidateNpcs
    : candidateNpcs.filter(
      (n) =>
        n.name.toLowerCase().includes(searchLower) ||
        n.race.toLowerCase().includes(searchLower) ||
        n.formId.toLowerCase().includes(searchLower) ||
        (n.pluginName && n.pluginName.toLowerCase().includes(searchLower)),
    )

  const handleSelectPartner = (npc: CompactNPC | { formId: string; name: string; race: string; gender?: CompactNPC['gender']; pluginName?: string }) => {
    if (npc.formId !== PLAYER_FORM_ID && !npcProfiles.has(npc.formId)) {
      updateNPCProfile(npc.formId, {
        formId: npc.formId,
        name: npc.name,
        race: npc.race,
        pluginName: npc.pluginName,
        gender: npc.gender ?? 'male',
      } as Partial<NPCProfile>)
    }
    setSelectedPartnerId(npc.formId === selectedPartnerId ? null : npc.formId)
  }

  const handleConfirmManual = () => {
    if (!selectedPartnerId || !canConfirmManual) return
    assignRelationshipStatus(targetNpcId, selectedPartnerId, selectedStatus, undefined, true)
    setOpen(false)
    setSelectedPartnerId(null)
  }

  return (
    <div className="fixed inset-0 z-50 bg-overlay flex items-center justify-center p-[1rem] select-none font-ui">
      <div className="bg-panel border border-border-strong rounded-lg shadow-window w-full max-w-[32rem] flex flex-col max-h-[36rem] overflow-hidden relative">
        {/* Header */}
        <header className="p-[1rem] bg-panel-raised border-b border-border-divider flex items-center justify-between">
          <div className="flex items-center gap-[0.5rem]">
            <HeartHandshake className="w-[1.25em] h-[1.25em] text-accent" />
            <div>
              <h2 className="text-base font-bold text-accent-bright">
                Matchmaking for {targetName}
              </h2>
              <span className="text-xs text-foreground-muted font-mono">{targetNpcId}</span>
            </div>
          </div>
          <button
            onClick={() => setOpen(false)}
            className="text-foreground-muted hover:text-accent p-[0.25rem] rounded hover:bg-accent-soft cursor-pointer transition-colors"
          >
            <X className="w-[1.25em] h-[1.25em]" />
          </button>
        </header>

        {/* Modal Tab Switcher */}
        <div className="flex border-b border-border-divider bg-panel-inset px-[1rem]">
          <button
            onClick={() => setActiveTab('manual')}
            className={`px-[1rem] py-[0.5rem] text-xs font-semibold border-b-2 flex items-center gap-[0.375rem] transition-colors cursor-pointer ${activeTab === 'manual'
              ? 'border-accent text-accent-bright bg-accent-soft/50'
              : 'border-transparent text-foreground-muted hover:text-foreground-primary'
              }`}
          >
            <Search className="w-[1em] h-[1em]" />
            <span>Manual Search</span>
          </button>

          {/* Explicitly gated SkyrimNet tab */}
          {isSkyrimNetEnabled && (
            <button
              onClick={() => setActiveTab('ai')}
              className={`px-[1rem] py-[0.5rem] text-xs font-semibold border-b-2 flex items-center gap-[0.375rem] transition-colors cursor-pointer ${activeTab === 'ai'
                ? 'border-accent text-accent-bright bg-accent-soft/50'
                : 'border-transparent text-foreground-muted hover:text-foreground-primary'
                }`}
            >
              <Bot className="w-[1em] h-[1em] text-accent" />
              <span>AI Suggest (SkyrimNet)</span>
            </button>
          )}
        </div>

        {/* Tab 1: Manual Search */}
        {activeTab === 'manual' && (
          <div className="flex-1 flex flex-col min-h-0 p-[1rem] gap-[0.75rem]">
            <div className="flex flex-col gap-[0.25rem]">
              <div className="flex items-center justify-between text-xs text-foreground-secondary">
                <span>Find Partner ({displayNpcs.length} available):</span>
                <button
                  onClick={() => fetchAllSkyrimNpcs()}
                  disabled={isLoadingAllNpcs}
                  title="Refresh Skyrim NPCs"
                  className="flex items-center gap-[0.25rem] text-[0.6875rem] text-foreground-muted hover:text-accent p-[0.125rem] rounded transition-colors cursor-pointer"
                >
                  <RefreshCw className={`w-[1em] h-[1em] ${isLoadingAllNpcs ? 'animate-spin text-accent' : ''}`} />
                  <span>Refresh</span>
                </button>
              </div>
              <SearchInput
                value={search}
                placeholder="Search candidate partner..."
                onChange={setSearch}
              />
            </div>

            {/* Virtualized Partner Candidate List */}
            <VirtualList
              items={displayNpcs}
              estimateSize={44}
              overscan={6}
              className="flex-1 min-h-0 border border-border-subtle rounded-md p-[0.375rem] bg-panel-inset overflow-y-auto"
              emptyMessage="No matching characters found."
              renderItem={(npc) => {
                const isSelected = selectedPartnerId === npc.formId
                return (
                  <div
                    onClick={() => handleSelectPartner(npc)}
                    className={`p-[0.5rem] rounded flex items-center justify-between text-xs cursor-pointer transition-all ${isSelected
                      ? 'bg-accent-soft text-accent-bright font-semibold border border-border-accent'
                      : 'bg-panel-raised hover:bg-interactive-hover text-foreground-primary border border-border-divider'
                      }`}
                  >
                    <div className="flex items-center gap-[0.375rem]">
                      <User className="w-[0.875em] h-[0.875em] text-foreground-muted" />
                      <div>
                        <div>{npc.name}</div>
                        <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem] flex-wrap">
                          <span className="font-mono">{npc.formId}</span>
                          {npc.pluginName && (
                            <>
                              <span>•</span>
                              <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${npc.pluginName}`}>
                                {npc.pluginName}
                              </span>
                            </>
                          )}
                        </div>
                      </div>
                    </div>
                    {isSelected && <Check className="w-[0.875em] h-[0.875em] text-accent" />}
                  </div>
                )
              }}
            />

            {/* Status Selection & Confirm */}
            {selectedPartnerId && (
              <div className="p-[0.75rem] bg-panel-raised rounded-md border border-border-accent space-y-[0.5rem]">
                <div className="flex items-center justify-between">
                  <span className="text-xs font-semibold text-foreground-primary">Assign Relationship Status:</span>
                  <span className="text-xs text-accent font-mono">
                    {candidateNpcs.find((n) => n.formId === selectedPartnerId)?.name || selectedPartnerId}
                  </span>
                </div>
                <div className="flex items-center gap-[0.5rem]">
                  <div className="flex-1">
                    <SelectInput
                      value={selectedStatus}
                      options={manualStatusOptions}
                      onChange={(val) => setSelectedStatus(parseInt(val))}
                      className="w-full text-xs"
                    />
                  </div>
                  <button
                    onClick={handleConfirmManual}
                    disabled={!canConfirmManual}
                    title={!canConfirmManual ? currentSelectedOption?.tooltip : undefined}
                    className={`px-[1rem] py-[0.375rem] font-semibold text-xs rounded transition-colors flex items-center gap-[0.25rem] ${canConfirmManual
                      ? 'bg-accent text-panel hover:bg-accent-bright cursor-pointer'
                      : 'bg-panel-inset text-foreground-disabled border border-border-subtle cursor-not-allowed opacity-50'
                      }`}
                  >
                    <Check className="w-[1em] h-[1em]" />
                    <span>Confirm Match</span>
                  </button>
                </div>
              </div>
            )}
          </div>
        )}

        {/* Tab 2: AI Suggest (SkyrimNet Gated) */}
        {activeTab === 'ai' && isSkyrimNetEnabled && (
          <div className="flex-1 flex flex-col min-h-0 p-[1rem] gap-[0.75rem]">
            {isGeneratingAi ? (
              <div className="flex-1 flex flex-col items-center justify-center p-[2rem] text-center text-foreground-muted gap-[0.5rem]">
                <Bot className="w-[2em] h-[2em] text-accent animate-pulse" />
                <span className="text-xs">Querying SkyrimNet AI for partner recommendations...</span>
                <button
                  type="button"
                  onClick={() => useMarasStore.setState({ isGeneratingAiSuggestions: false })}
                  className="mt-[0.5rem] px-[0.625rem] py-[0.25rem] rounded text-xs text-foreground-muted hover:text-foreground-primary bg-panel-inset hover:bg-interactive-hover border border-border-subtle cursor-pointer transition-colors"
                >
                  Cancel
                </button>
              </div>
            ) : aiSuggestions.length === 0 ? (
              <div className="flex-1 flex flex-col items-center justify-center p-[2rem] text-center text-foreground-muted gap-[0.75rem] bg-panel-inset border border-border-subtle rounded-md">
                <Bot className="w-[2em] h-[2em] text-accent/50" />
                <span className="text-xs">No suggestions generated yet. Click below to query SkyrimNet AI.</span>
                <button
                  onClick={() => generateAiSuggestions(targetNpcId)}
                  disabled={isGeneratingAi}
                  className="px-[0.875rem] py-[0.375rem] bg-accent hover:bg-accent-bright text-panel font-semibold text-xs rounded transition-colors cursor-pointer flex items-center gap-[0.375rem]"
                >
                  <Sparkles className="w-[0.875em] h-[0.875em]" />
                  <span>Generate AI Suggestions</span>
                </button>
              </div>
            ) : (
              <>
                <div className="flex items-center justify-between text-xs text-foreground-secondary bg-panel-inset p-[0.5rem] rounded border border-border-subtle">
                  <span>SkyrimNet AI context recommendations:</span>
                  <button
                    onClick={() => generateAiSuggestions(targetNpcId)}
                    disabled={isGeneratingAi}
                    className="px-[0.5rem] py-[0.25rem] bg-accent-soft text-accent-bright hover:bg-accent/20 rounded border border-border-accent flex items-center gap-[0.25rem] transition-colors cursor-pointer text-xs"
                  >
                    <Sparkles className="w-[0.875em] h-[0.875em]" />
                    <span>{isGeneratingAi ? 'Generating...' : 'Regenerate'}</span>
                  </button>
                </div>
                <div className="flex-1 min-h-0 overflow-y-auto border border-border-subtle rounded-md p-[0.5rem] bg-panel-inset space-y-[0.5rem] pr-[0.25rem]">
                  {aiSuggestions.map((s) => {
                    const npc = npcProfiles.get(s.formId) || allSkyrimNpcs.find((n) => n.formId === s.formId)
                    const name = s.name || npc?.name || s.formId

                    return (
                      <div
                        key={s.formId}
                        className={`p-[0.625rem] rounded-md border flex flex-col gap-[0.375rem] transition-all ${s.selected
                          ? 'bg-panel-raised border-border-accent'
                          : 'bg-panel-inset border-border-divider opacity-60'
                          }`}
                      >
                        <div className="flex items-center justify-between gap-[0.75rem]">
                          <div className="flex items-center gap-[0.5rem]">
                            <button
                              type="button"
                              onClick={() => toggleAiSuggestionSelected(s.formId)}
                              className={`w-[1.125rem] h-[1.125rem] rounded flex items-center justify-center transition-all cursor-pointer border flex-shrink-0 ${s.selected
                                ? 'bg-accent border-accent text-panel font-bold shadow-sm'
                                : 'bg-panel-inset border-border-divider hover:border-accent/60 text-transparent'
                                }`}
                            >
                              <Check className="w-[0.75rem] h-[0.75rem] stroke-[3]" />
                            </button>
                            <div>
                              <div className="text-xs font-semibold text-foreground-primary flex items-center gap-[0.375rem]">
                                <span>{name}</span>
                                {s.action && (
                                  <span className="px-[0.25rem] py-[0.0625rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-accent-soft text-accent-bright border border-border-accent">
                                    {s.action}
                                  </span>
                                )}
                              </div>
                              <div className="text-[0.6875rem] text-foreground-muted font-mono">{s.formId}</div>
                            </div>
                          </div>

                          {(() => {
                            const suggestionOptions = getAvailableStatuses(targetNpcId, s.formId)
                            const loverOpt = suggestionOptions.find((o) => o.status === RelationshipStatus.Lover)
                            const isCheating = loverOpt?.loverType === 'cheating'
                            return (
                              <div className="flex items-center gap-[0.375rem]">
                                {(s.targetStatus?.toLowerCase() === 'lover' || s.suggestedStatus === RelationshipStatus.Lover) && (
                                  isCheating ? (
                                    <span className="px-[0.375rem] py-[0.0625rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-rose-950/60 text-rose-400 border border-rose-800/80" title="Considered cheating: at least one partner is in a closed main relationship">
                                      Cheating
                                    </span>
                                  ) : (
                                    <span className="px-[0.375rem] py-[0.0625rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-emerald-950/60 text-emerald-400 border border-emerald-800/80" title="Allowed secondary relationship: both parties are single or in open relationships">
                                      Allowed
                                    </span>
                                  )
                                )}
                                <div className="w-[8.5rem]">
                                  <SelectInput
                                    value={s.suggestedStatus}
                                    options={suggestionOptions}
                                    onChange={(val) => updateAiSuggestionStatus(s.formId, parseInt(val))}
                                    className="w-full text-xs"
                                  />
                                </div>
                              </div>
                            )
                          })()}
                        </div>

                        {/* Dual-POV Memories Section (SkyrimNet Integration) */}
                        {s.isGeneratingMemory ? (
                          <div className="flex items-center gap-[0.5rem] text-[0.6875rem] text-accent-bright bg-accent-soft p-[0.5rem] rounded border border-border-accent animate-pulse">
                            <Bot className="w-[1.125em] h-[1.125em] animate-spin" />
                            <span>Generating dual-POV recollections via SkyrimNet AI...</span>
                          </div>
                        ) : s.dualMemories && s.dualMemories.length > 0 ? (
                          <div className="space-y-[0.375rem]">
                            <div className="flex items-center justify-between text-[0.625rem] font-semibold text-accent-bright">
                              <span className="flex items-center gap-[0.25rem]">
                                <BookOpen className="w-[0.875em] h-[0.875em]" />
                                <span>Dual-POV Recollections:</span>
                              </span>
                              <button
                                type="button"
                                onClick={() => generateMatchMemories(targetNpcId || '', s.formId, s.targetStatus || 'dating', s.action)}
                                className="text-foreground-secondary hover:text-accent-bright flex items-center gap-[0.25rem] cursor-pointer transition-colors"
                              >
                                <Sparkles className="w-[0.75em] h-[0.75em]" />
                                <span>Regenerate</span>
                              </button>
                            </div>
                            <div className="grid grid-cols-1 md:grid-cols-2 gap-[0.375rem]">
                              {s.dualMemories.map((mem, memIdx) => (
                                <div
                                  key={memIdx}
                                  className="bg-panel-inset p-[0.5rem] rounded border border-border-subtle flex flex-col justify-between gap-[0.25rem] text-[0.6875rem]"
                                >
                                  <div>
                                    <div className="flex items-center justify-between mb-[0.125rem]">
                                      <span className="font-bold text-accent-bright flex items-center gap-[0.25rem]">
                                        <User className="w-[0.75em] h-[0.75em]" />
                                        <span>{mem.actor || (memIdx === 0 ? 'Target' : name)}'s POV</span>
                                      </span>
                                      {mem.emotion && (
                                        <span className="px-[0.25rem] py-[0.03125rem] rounded bg-accent-soft text-accent-bright font-mono uppercase text-[0.5625rem] border border-border-accent/40">
                                          {mem.emotion}
                                        </span>
                                      )}
                                    </div>
                                    <p className="italic text-foreground-primary leading-relaxed">
                                      "{mem.content}"
                                    </p>
                                  </div>
                                  <div className="pt-[0.125rem] border-t border-border-divider/50 flex items-center justify-between text-[0.5625rem] text-foreground-muted">
                                    <span className="truncate max-w-[8rem]">{mem.location || 'Skyrim'}</span>
                                    {mem.importance_score !== undefined && (
                                      <span className="font-mono text-accent">Score: {(mem.importance_score * 100).toFixed(0)}%</span>
                                    )}
                                  </div>
                                </div>
                              ))}
                            </div>
                          </div>
                        ) : (
                          <div className="flex items-center justify-between text-[0.6875rem] bg-panel-inset p-[0.375rem] rounded border border-border-subtle">
                            <span className="text-foreground-muted flex items-center gap-[0.25rem]">
                              <BookOpen className="w-[0.875em] h-[0.875em] text-foreground-secondary" />
                              <span>No memories generated yet.</span>
                            </span>
                            <button
                              type="button"
                              onClick={() => generateMatchMemories(targetNpcId || '', s.formId, s.targetStatus || 'dating', s.action)}
                              className="px-[0.5rem] py-[0.1875rem] bg-accent-soft hover:bg-accent/20 text-accent-bright border border-border-accent font-semibold rounded transition-colors cursor-pointer flex items-center gap-[0.25rem] text-[0.6875rem]"
                            >
                              <Sparkles className="w-[0.75em] h-[0.75em]" />
                              <span>Generate Memories</span>
                            </button>
                          </div>
                        )}
                      </div>
                    )
                  })}
                </div>
              </>
            )}

            {aiSuggestions.length > 0 && !isGeneratingAi && (
              <div className="flex justify-end pt-[0.25rem] border-t border-border-divider">
                <button
                  onClick={() => applyAiSuggestions(targetNpcId)}
                  className="px-[1rem] py-[0.375rem] bg-accent text-panel font-semibold text-xs rounded hover:bg-accent-bright transition-colors cursor-pointer flex items-center gap-[0.25rem]"
                >
                  <Check className="w-[1em] h-[1em]" />
                  <span>Apply Selected Suggestions</span>
                </button>
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  )
}
