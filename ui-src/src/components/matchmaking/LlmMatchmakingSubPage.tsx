import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus, MatchmakingOrientation } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { SearchInput } from '../shared/SearchInput'
import { SelectInput } from '../shared/SelectInput'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { VirtualList } from '../shared/VirtualList'
import { getAvailableStatuses } from '../../utils/statusAvailability'
import {
  Bot,
  Sparkles,
  User,
  Check,
  HeartHandshake,
  Sliders,
  Award,
  BookOpen,
  Calendar,
  Settings2,
  Globe,
} from 'lucide-react'

export const LlmMatchmakingSubPage: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const setActiveTab = useMarasStore((s) => s.setActiveTab)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const matchmakingSettings = useMarasStore((s) => s.matchmakingSettings)
  const updateMatchmakingSettings = useMarasStore((s) => s.updateMatchmakingSettings)

  const aiSuggestions = useMarasStore((s) => s.aiSuggestions)
  const matchmakingScanType = useMarasStore((s) => s.matchmakingScanType)
  const isGeneratingAi = useMarasStore((s) => s.isGeneratingAiSuggestions)
  const generateAiSuggestions = useMarasStore((s) => s.generateAiSuggestions)
  const generateGlobalAiSuggestions = useMarasStore((s) => s.generateGlobalAiSuggestions)
  const generateMatchMemories = useMarasStore((s) => s.generateMatchMemories)
  const toggleAiSuggestionSelected = useMarasStore((s) => s.toggleAiSuggestionSelected)
  const updateAiSuggestionStatus = useMarasStore((s) => s.updateAiSuggestionStatus)
  const applyAiSuggestions = useMarasStore((s) => s.applyAiSuggestions)
  const assignRelationshipStatus = useMarasStore((s) => s.assignRelationshipStatus)
  const removeRelationship = useMarasStore((s) => s.removeRelationship)
  const setPairIsOpen = useMarasStore((s) => s.setPairIsOpen)

  const allSkyrimNpcs = useMarasStore((s) => s.allSkyrimNpcs)

  // Candidate Target NPCs (Adults only, player excluded for NPC matchmaking)
  const allNpcs = allSkyrimNpcs.filter((n) => !n.isChild && n.formId !== PLAYER_FORM_ID)

  const [targetNpcId, setTargetNpcId] = useState<string>(() => allNpcs[0]?.formId || '')
  const [targetSearch, setTargetSearch] = useState('')

  const isSkyrimNetEnabled = Boolean(settings.hasSkyrimNet)

  const filteredTargetNpcs = allNpcs.filter(
    (n) =>
      n.name.toLowerCase().includes(targetSearch.toLowerCase()) ||
      n.formId.toLowerCase().includes(targetSearch.toLowerCase()) ||
      n.race.toLowerCase().includes(targetSearch.toLowerCase()) ||
      (n.pluginName && n.pluginName.toLowerCase().includes(targetSearch.toLowerCase())),
  )

  const targetProfile = npcProfiles.get(targetNpcId) || allNpcs.find((n) => n.formId === targetNpcId)

  const isGlobalScan =
    matchmakingScanType === 'global' ||
    (matchmakingScanType !== 'target' &&
      aiSuggestions.length > 0 &&
      aiSuggestions.some((s) => s.actorAFormId && s.actorAFormId !== targetNpcId))

  const handleGenerate = () => {
    if (!isSkyrimNetEnabled || !targetNpcId) return
    generateAiSuggestions(targetNpcId)
  }

  const handleSelectAll = (selected: boolean) => {
    aiSuggestions.forEach((s) => {
      if (s.selected !== selected) {
        toggleAiSuggestionSelected(s.formId)
      }
    })
  }

  const getActionBadge = (action?: string) => {
    const act = (action || 'new').toLowerCase()
    if (act === 'new' || act === 'create') {
      return (
        <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-emerald-950/60 text-emerald-400 border border-emerald-800/80">
          New
        </span>
      )
    }
    if (act === 'advance') {
      return (
        <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-blue-950/60 text-blue-400 border border-blue-800/80">
          Advance
        </span>
      )
    }
    if (act === 'breakup') {
      return (
        <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-rose-950/60 text-rose-400 border border-rose-800/80">
          Breakup
        </span>
      )
    }
    if (act === 'open') {
      return (
        <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-amber-950/60 text-amber-400 border border-amber-800/80">
          Open Pair
        </span>
      )
    }
    if (act === 'close') {
      return (
        <span className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-purple-950/60 text-purple-400 border border-purple-800/80">
          Close Pair
        </span>
      )
    }
    return null
  }

  return (
    <div className="flex h-full w-full gap-[1rem] overflow-hidden select-none font-ui">
      {/* Left Column: Target NPC & LLM Tuning Config */}
      <div className="w-[23rem] flex flex-col gap-[0.75rem] bg-panel border border-border-divider rounded-lg p-[1rem] overflow-hidden flex-shrink-0">
        {/* Header */}
        <div className="flex items-center gap-[0.5rem] pb-[0.5rem] border-b border-border-divider">
          <Bot className="w-[1.25em] h-[1.25em] text-accent" />
          <div>
            <h2 className="text-sm font-bold text-accent-bright">LLM Matchmaking Setup</h2>
            <p className="text-[0.6875rem] text-foreground-muted">SkyrimNet NPC-NPC Matchmaker</p>
          </div>
        </div>

        {/* Target NPC Selection */}
        <div className="space-y-[0.375rem] flex-1 flex flex-col min-h-0">
          <label className="text-xs font-semibold text-foreground-secondary flex items-center justify-between">
            <span>Target NPC:</span>
            <span className="text-[0.6875rem] text-accent font-mono truncate max-w-[9rem]">
              {targetProfile?.name || targetNpcId || 'None'}
            </span>
          </label>

          <SearchInput
            value={targetSearch}
            placeholder="Search target character..."
            onChange={setTargetSearch}
          />

          <VirtualList
            items={filteredTargetNpcs}
            estimateSize={48}
            overscan={6}
            className="flex-1 min-h-0 border border-border-subtle rounded-md p-[0.375rem] bg-panel-inset overflow-y-auto"
            emptyMessage="No matching characters found."
            renderItem={(npc) => {
              const isSelected = targetNpcId === npc.formId
              return (
                <div
                  onClick={() => {
                    if (targetNpcId !== npc.formId) {
                      setTargetNpcId(npc.formId)
                      useMarasStore.setState({ aiSuggestions: [], matchmakingScanType: 'target' })
                    }
                  }}
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
        </div>

        {/* Action Button */}
        <button
          onClick={handleGenerate}
          disabled={!isSkyrimNetEnabled || isGeneratingAi || !targetNpcId}
          className={`w-full py-[0.625rem] px-[1rem] rounded-md font-semibold text-xs flex items-center justify-center gap-[0.5rem] transition-all cursor-pointer ${!isSkyrimNetEnabled || !targetNpcId
              ? 'bg-panel-inset text-foreground-muted cursor-not-allowed border border-border-divider'
              : isGeneratingAi
                ? 'bg-accent/50 text-accent-bright animate-pulse cursor-wait'
                : 'bg-accent hover:bg-accent-bright text-panel shadow-md'
            }`}
        >
          <Sparkles className="w-[1em] h-[1em]" />
          <span>{isGeneratingAi ? 'Analyzing Context...' : 'Generate Target Matches'}</span>
        </button>

        {/* Global Matchmaking Scan */}
        <div className="pt-[0.5rem] border-t border-border-divider flex flex-col gap-[0.375rem]">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-foreground-secondary">Global Matchmaker:</span>
            <span className="text-[0.625rem] text-foreground-muted">All Skyrim NPCs</span>
          </div>
          <button
            type="button"
            onClick={() => generateGlobalAiSuggestions(false)}
            disabled={!isSkyrimNetEnabled || isGeneratingAi}
            className={`w-full py-[0.5rem] px-[0.75rem] rounded-md font-semibold text-xs flex items-center justify-center gap-[0.375rem] transition-all cursor-pointer border ${!isSkyrimNetEnabled
                ? 'bg-panel-inset text-foreground-muted cursor-not-allowed border-border-divider'
                : isGeneratingAi
                  ? 'bg-accent/50 text-accent-bright animate-pulse cursor-wait border-border-accent'
                  : 'bg-panel-inset hover:bg-interactive-hover text-foreground-primary border-border-subtle hover:border-accent'
              }`}
          >
            <Globe className="w-[1em] h-[1em] text-accent" />
            <span>Scan Any NPCs (World Scan)</span>
          </button>
        </div>

        {/* Quick hint for Settings */}
        <div className="pt-[0.25rem] text-center">
          <button
            type="button"
            onClick={() => {
              setActiveTab('settings')
              setActiveSubSection('matchmaking')
            }}
            className="text-[0.6875rem] text-accent hover:text-accent-bright hover:underline inline-flex items-center gap-[0.25rem] cursor-pointer"
          >
            <Settings2 className="w-[0.875em] h-[0.875em]" />
            Configure Matchmaking Rules in Settings
          </button>
        </div>
      </div>

      {/* Right Column: AI Results Matrix */}
      <div className="flex-1 flex flex-col gap-[0.75rem] bg-panel border border-border-divider rounded-lg p-[1rem] overflow-hidden">
        {/* Header */}
        <div className="flex items-center justify-between pb-[0.5rem] border-b border-border-divider gap-[0.75rem]">
          <div className="flex items-center gap-[0.5rem] min-w-0">
            <HeartHandshake className="w-[1.25em] h-[1.25em] text-accent flex-shrink-0" />
            <div className="min-w-0">
              <h2 className="text-sm font-bold text-accent-bright truncate">
                SkyrimNet Recommendations
              </h2>
              <p className="text-[0.6875rem] text-foreground-muted truncate">
                {isGlobalScan ? (
                  <span>Showing <span className="font-semibold text-foreground-primary">World Matchmaking Scan</span> across Skyrim NPCs</span>
                ) : (
                  <span>Showing partner evaluation for <span className="font-semibold text-foreground-primary">{targetProfile?.name || targetNpcId}</span></span>
                )}
              </p>
            </div>
          </div>

          {aiSuggestions.length > 0 && (
            <div className="flex items-center gap-[0.5rem] flex-shrink-0">
              <button
                type="button"
                onClick={() => handleSelectAll(aiSuggestions.some((s) => !s.selected))}
                className="px-[0.625rem] py-[0.25rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded text-xs text-foreground-secondary cursor-pointer transition-colors whitespace-nowrap flex-shrink-0"
              >
                {aiSuggestions.every((s) => s.selected) ? 'Deselect All' : 'Select All'}
              </button>
              <button
                type="button"
                onClick={() => applyAiSuggestions(targetNpcId)}
                disabled={!aiSuggestions.some((s) => s.selected)}
                className="px-[0.75rem] py-[0.25rem] bg-accent hover:bg-accent-bright disabled:opacity-50 text-panel font-semibold text-xs rounded transition-colors flex items-center gap-[0.25rem] cursor-pointer whitespace-nowrap flex-shrink-0"
              >
                <Check className="w-[0.875em] h-[0.875em]" />
                <span>Apply Selected</span>
              </button>
            </div>
          )}
        </div>

        {/* Results Body */}
        {isGeneratingAi ? (
          <div className="flex-1 flex flex-col items-center justify-center p-[2rem] text-center gap-[0.75rem]">
            <Bot className="w-[2.5em] h-[2.5em] text-accent animate-pulse" />
            <div>
              <div className="text-sm font-bold text-foreground-primary">
                Querying SkyrimNet AI Context...
              </div>
              <div className="text-xs text-foreground-muted mt-[0.25rem]">
                Evaluating character lore, relationships, hold diversity, and romance compatibility.
              </div>
            </div>
            <div className="flex items-center gap-[0.5rem]">
              <div className="w-2 h-2 rounded-full bg-accent animate-bounce" />
              <div className="w-2 h-2 rounded-full bg-accent animate-bounce [animation-delay:0.2s]" />
              <div className="w-2 h-2 rounded-full bg-accent animate-bounce [animation-delay:0.4s]" />
            </div>
            <div className="pt-[0.5rem]">
              <button
                type="button"
                onClick={() => useMarasStore.setState({ isGeneratingAiSuggestions: false })}
                className="px-[0.75rem] py-[0.25rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded text-xs text-foreground-secondary cursor-pointer transition-colors"
              >
                Cancel
              </button>
            </div>
          </div>
        ) : aiSuggestions.length === 0 ? (
          <div className="flex-1 flex flex-col items-center justify-center p-[2rem] text-center gap-[0.75rem] bg-panel-inset border border-border-subtle rounded-lg text-foreground-muted">
            <Sparkles className="w-[2.5em] h-[2.5em] text-accent/50" />
            <div>
              <div className="text-sm font-semibold text-foreground-primary">No LLM Match Analysis Generated Yet</div>
              <div className="text-xs max-w-[22rem] mt-[0.25rem]">
                Select a target character on the left or click <span className="text-accent font-semibold">Scan Any NPCs (World Scan)</span> to run SkyrimNet evaluation.
              </div>
            </div>
          </div>
        ) : (
          <div className="flex-1 min-h-0 overflow-y-auto pr-[0.375rem] space-y-[0.75rem]">
            {aiSuggestions.map((s) => {
              const candidateProfile = npcProfiles.get(s.formId) || allNpcs.find((n) => n.formId === s.formId)
              const profileA = s.actorAFormId ? (npcProfiles.get(s.actorAFormId) || allNpcs.find((n) => n.formId === s.actorAFormId)) : undefined
              const candidateName = s.name || candidateProfile?.name || s.formId
              const score = s.compatibilityScore || 85
              const memory = s.memory || s.reasoning || 'Favorable romantic connection identified by SkyrimNet.'
              const tags = s.synergyTags || ['#RomanticSynergy', '#SkyrimLore']

              const primaryActor = s.actorAFormId || targetNpcId
              const suggestionOptions = getAvailableStatuses(primaryActor, s.formId)
              const loverOpt = suggestionOptions.find((o) => o.status === RelationshipStatus.Lover)
              const isLoverCheating = loverOpt?.loverType === 'cheating'
              const currentStatusOpt = suggestionOptions.find((o) => o.value === s.suggestedStatus)
              const canApply =
                s.action === 'open' ||
                s.action === 'close' ||
                s.action === 'breakup' ||
                (currentStatusOpt?.enabled ?? false)

              return (
                <div
                  key={s.formId}
                  className={`p-[0.875rem] rounded-lg border flex flex-col gap-[0.625rem] transition-all ${s.selected
                      ? 'bg-panel-raised border-border-accent shadow-sm'
                      : 'bg-panel-inset border-border-divider opacity-70'
                    }`}
                >
                  {/* Top Row: Checkbox, Partners & Badges */}
                  <div className="flex items-start justify-between gap-[0.75rem]">
                    {/* Left: Checkbox + Partner(s) */}
                    <div className="flex items-start gap-[0.625rem] min-w-0 flex-1">
                      <button
                        type="button"
                        onClick={() => toggleAiSuggestionSelected(s.formId)}
                        className={`mt-[0.1875rem] w-[1.25rem] h-[1.25rem] rounded flex items-center justify-center transition-all cursor-pointer border flex-shrink-0 ${s.selected
                            ? 'bg-accent border-accent text-panel font-bold shadow-sm'
                            : 'bg-panel-inset border-border-divider hover:border-accent/60 text-transparent'
                          }`}
                      >
                        <Check className="w-[0.875rem] h-[0.875rem] stroke-[3]" />
                      </button>

                      <div className="min-w-0 flex-1 space-y-[0.375rem]">
                        {isGlobalScan && s.actorAName ? (
                          /* Dual-Character Pairing Layout (World Scan) */
                          <div className="flex items-center gap-[0.375rem] flex-wrap">
                            {/* Partner A */}
                            <div className="inline-flex items-center gap-[0.375rem] bg-panel-inset px-[0.5rem] py-[0.25rem] rounded border border-border-subtle max-w-full">
                              <User className="w-[0.875em] h-[0.875em] text-accent flex-shrink-0" />
                              <span className="text-xs font-bold text-accent truncate">{s.actorAName}</span>
                              {s.actorAFormId && (
                                <span className="text-[0.625rem] text-foreground-muted font-mono font-normal flex-shrink-0">
                                  {s.actorAFormId}
                                </span>
                              )}
                              {profileA?.race && (
                                <span className="text-[0.625rem] text-foreground-secondary border-l border-border-divider pl-[0.375rem] flex-shrink-0">
                                  {profileA.race}
                                </span>
                              )}
                            </div>

                            <span className="text-accent-bright font-bold text-xs flex-shrink-0">↔</span>

                            {/* Partner B */}
                            <div className="inline-flex items-center gap-[0.375rem] bg-panel-inset px-[0.5rem] py-[0.25rem] rounded border border-border-subtle max-w-full">
                              <User className="w-[0.875em] h-[0.875em] text-accent flex-shrink-0" />
                              <span className="text-xs font-bold text-foreground-primary truncate">{candidateName}</span>
                              <span className="text-[0.625rem] text-foreground-muted font-mono font-normal flex-shrink-0">
                                {s.formId}
                              </span>
                              {candidateProfile?.race && (
                                <span className="text-[0.625rem] text-foreground-secondary border-l border-border-divider pl-[0.375rem] flex-shrink-0">
                                  {candidateProfile.race}
                                </span>
                              )}
                            </div>
                          </div>
                        ) : (
                          /* Single Candidate Layout (Target Mode) */
                          <div className="flex items-center gap-[0.375rem] flex-wrap">
                            <User className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                            <span className="text-sm font-bold text-foreground-primary">{candidateName}</span>
                            <span className="text-[0.6875rem] text-foreground-muted font-mono font-normal">
                              ({s.formId})
                            </span>
                            <span className="text-[0.6875rem] text-foreground-secondary">
                              • {candidateProfile?.race || 'Skyrim NPC'}
                            </span>
                            {candidateProfile?.pluginName && (
                              <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle">
                                {candidateProfile.pluginName}
                              </span>
                            )}
                          </div>
                        )}
                      </div>
                    </div>

                    {/* Right: Badges cluster */}
                    <div className="flex items-center gap-[0.375rem] flex-shrink-0">
                      {getActionBadge(s.action)}

                      {(s.targetStatus?.toLowerCase() === 'lover' || s.suggestedStatus === RelationshipStatus.Lover) && (
                        isLoverCheating ? (
                          <span
                            className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-rose-950/60 text-rose-400 border border-rose-800/80"
                            title="Considered cheating: at least one partner is in a closed main relationship"
                          >
                            Cheating
                          </span>
                        ) : (
                          <span
                            className="px-[0.375rem] py-[0.125rem] rounded text-[0.625rem] font-bold uppercase tracking-wider bg-emerald-950/60 text-emerald-400 border border-emerald-800/80"
                            title="Allowed secondary relationship: both parties are single or in open relationships"
                          >
                            Allowed
                          </span>
                        )
                      )}

                      <div className="px-[0.5rem] py-[0.1875rem] rounded-full border text-[0.6875rem] font-bold flex items-center gap-[0.25rem] text-accent-bright bg-accent-soft border-border-accent flex-shrink-0">
                        <Award className="w-[0.875em] h-[0.875em]" />
                        <span>{s.targetStatus ? s.targetStatus.charAt(0).toUpperCase() + s.targetStatus.slice(1) : 'Dating'}</span>
                      </div>
                    </div>
                  </div>

                  {/* Dual-POV Memories Section (SkyrimNet Integration) */}
                  {s.isGeneratingMemory ? (
                    <div className="flex items-center gap-[0.5rem] text-xs text-accent-bright bg-accent-soft p-[0.625rem] rounded border border-border-accent animate-pulse">
                      <Bot className="w-[1.25em] h-[1.25em] animate-spin" />
                      <span>Generating first-person memories from both characters' perspectives via SkyrimNet AI...</span>
                    </div>
                  ) : s.dualMemories && s.dualMemories.length > 0 ? (
                    <div className="space-y-[0.375rem]">
                      <div className="flex items-center justify-between text-[0.6875rem] font-semibold text-accent-bright">
                        <span className="flex items-center gap-[0.25rem]">
                          <BookOpen className="w-[1em] h-[1em]" />
                          <span>Dual-POV Recollections (SkyrimNet Memory Records):</span>
                        </span>
                        <button
                          type="button"
                          onClick={() => generateMatchMemories(s.actorAFormId || targetNpcId, s.formId, s.targetStatus || 'dating', s.action)}
                          className="text-[0.625rem] text-foreground-secondary hover:text-accent-bright flex items-center gap-[0.25rem] cursor-pointer transition-colors"
                        >
                          <Sparkles className="w-[0.75em] h-[0.75em]" />
                          <span>Regenerate Memories</span>
                        </button>
                      </div>
                      <div className="grid grid-cols-1 md:grid-cols-2 gap-[0.5rem]">
                        {s.dualMemories.map((mem, memIdx) => (
                          <div
                            key={memIdx}
                            className="bg-panel-inset p-[0.625rem] rounded border border-border-subtle flex flex-col justify-between gap-[0.375rem] text-xs"
                          >
                            <div>
                              <div className="flex items-center justify-between mb-[0.25rem] text-[0.6875rem]">
                                <span className="font-bold text-accent-bright flex items-center gap-[0.25rem]">
                                  <User className="w-[0.875em] h-[0.875em]" />
                                  <span>{mem.actor || (memIdx === 0 ? (s.actorAName || targetProfile?.name) : candidateProfile?.name)}'s POV</span>
                                </span>
                                {mem.emotion && (
                                  <span className="px-[0.375rem] py-[0.0625rem] rounded bg-accent-soft text-accent-bright font-mono uppercase text-[0.625rem] border border-border-accent/40">
                                    {mem.emotion}
                                  </span>
                                )}
                              </div>
                              <p className="italic text-foreground-primary leading-relaxed text-[0.6875rem]">
                                "{mem.content}"
                              </p>
                            </div>
                            <div className="pt-[0.25rem] border-t border-border-divider/50 flex items-center justify-between text-[0.625rem] text-foreground-muted">
                              <span className="truncate max-w-[12rem]">{mem.location || 'Skyrim'}</span>
                              {mem.importance_score !== undefined && (
                                <span className="font-mono text-accent">Score: {(mem.importance_score * 100).toFixed(0)}%</span>
                              )}
                            </div>
                            {mem.tags && mem.tags.length > 0 && (
                              <div className="flex items-center gap-[0.25rem] flex-wrap mt-[0.125rem]">
                                {mem.tags.slice(0, 5).map((t, ti) => (
                                  <span key={ti} className="text-[0.5625rem] px-[0.25rem] py-[0.03125rem] rounded bg-panel text-foreground-muted font-mono border border-border-subtle/50">
                                    #{t.replace(/^#/, '')}
                                  </span>
                                ))}
                              </div>
                            )}
                          </div>
                        ))}
                      </div>
                    </div>
                  ) : (
                    <div className="flex items-center justify-between text-xs bg-panel-inset p-[0.5rem] rounded border border-border-subtle">
                      <span className="text-foreground-muted flex items-center gap-[0.375rem]">
                        <BookOpen className="w-[1em] h-[1em] text-foreground-secondary" />
                        <span>No memories generated yet for this pairing.</span>
                      </span>
                      <button
                        type="button"
                        onClick={() => generateMatchMemories(s.actorAFormId || targetNpcId, s.formId, s.targetStatus || 'dating', s.action)}
                        className="px-[0.625rem] py-[0.25rem] bg-accent-soft hover:bg-accent/20 text-accent-bright border border-border-accent font-semibold rounded transition-colors cursor-pointer flex items-center gap-[0.375rem] text-xs shadow-sm"
                      >
                        <Sparkles className="w-[0.875em] h-[0.875em]" />
                        <span>Generate Memories</span>
                      </button>
                    </div>
                  )}

                  {/* Bottom Row: Synergy Tags, Status Selector & Assign Action */}
                  <div className="flex items-center justify-between pt-[0.375rem] border-t border-border-divider text-xs gap-[0.5rem] flex-wrap">
                    {/* Tags */}
                    <div className="flex items-center gap-[0.375rem] flex-wrap min-w-0">
                      {tags.map((tag, i) => (
                        <span
                          key={i}
                          className="px-[0.375rem] py-[0.125rem] rounded bg-accent-soft text-accent-bright text-[0.6875rem] font-medium"
                        >
                          {tag}
                        </span>
                      ))}
                    </div>

                    {/* Status Selector & Direct Assign */}
                    <div className="flex items-center gap-[0.5rem] flex-shrink-0 ml-auto">
                      <span className="text-foreground-muted text-[0.6875rem]">Status:</span>
                      <div className="w-[7.5rem]">
                        <SelectInput
                          value={s.suggestedStatus}
                          options={suggestionOptions}
                          onChange={(val) => updateAiSuggestionStatus(s.formId, parseInt(val))}
                          className="w-full text-xs"
                        />
                      </div>
                      <button
                        onClick={() => {
                          const primaryActor = s.actorAFormId || targetNpcId
                          if (!primaryActor || !canApply) return
                          if (s.action === 'open') {
                            setPairIsOpen(primaryActor, s.formId, true, true)
                          } else if (s.action === 'close') {
                            setPairIsOpen(primaryActor, s.formId, false, true)
                          } else if (s.action === 'breakup') {
                            removeRelationship(primaryActor, s.formId, true)
                          } else {
                            assignRelationshipStatus(primaryActor, s.formId, s.suggestedStatus, s.dualMemories, true)
                          }
                        }}
                        disabled={!canApply}
                        title={!canApply ? currentStatusOpt?.tooltip : undefined}
                        className={`px-[0.625rem] py-[0.25rem] rounded font-semibold text-xs transition-colors flex items-center gap-[0.25rem] whitespace-nowrap ${canApply
                            ? 'bg-accent-soft hover:bg-accent/20 text-accent-bright border border-border-accent cursor-pointer'
                            : 'bg-panel-inset text-foreground-disabled border border-border-subtle cursor-not-allowed opacity-50'
                          }`}
                      >
                        <span>Apply Match</span>
                      </button>
                    </div>
                  </div>
                </div>
              )
            })}
          </div>
        )}
      </div>
    </div>
  )
}
