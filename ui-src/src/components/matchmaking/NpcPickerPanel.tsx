import React, { useState, useEffect } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { SearchInput } from '../shared/SearchInput'
import { getStatusLabel } from '../shared/StatusBadge'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { VirtualList } from '../shared/VirtualList'
import { Crown, User, Heart, RefreshCw } from 'lucide-react'
import { NPCProfile, CompactNPC } from '../../types'

interface NpcPickerPanelProps {
  side: 'a' | 'b'
  title: string
}

export const NpcPickerPanel: React.FC<NpcPickerPanelProps> = ({ side, title }) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const allSkyrimNpcs = useMarasStore((s) => s.allSkyrimNpcs)
  const isLoadingAllNpcs = useMarasStore((s) => s.isLoadingAllNpcs)
  const fetchAllSkyrimNpcs = useMarasStore((s) => s.fetchAllSkyrimNpcs)

  const selectedA = useMarasStore((s) => s.matchmakingNpcA)
  const selectedB = useMarasStore((s) => s.matchmakingNpcB)
  const setNpcA = useMarasStore((s) => s.setMatchmakingNpcA)
  const setNpcB = useMarasStore((s) => s.setMatchmakingNpcB)

  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)

  const [searchInput, setSearchInput] = useState('')

  const selectedId = side === 'a' ? selectedA : selectedB
  const otherSelectedId = side === 'a' ? selectedB : selectedA
  const setSelected = side === 'a' ? setNpcA : setNpcB

  // Auto-fetch Skyrim NPCs if not loaded
  useEffect(() => {
    if (allSkyrimNpcs.length === 0) {
      fetchAllSkyrimNpcs()
    }
  }, [allSkyrimNpcs.length, fetchAllSkyrimNpcs])

  const playerProfile = npcProfiles.get(PLAYER_FORM_ID)
  const playerName = playerProfile?.name || 'Prisoner (Player)'
  const playerRace = playerProfile?.race || 'Nord'
  const playerGender = playerProfile?.gender || 'female'

  // Select list data source
  const baseCandidates: Array<CompactNPC | { formId: string; name: string; race: string; gender?: CompactNPC['gender']; isChild?: boolean; pluginName?: string }> = [
    { formId: PLAYER_FORM_ID, name: playerName, race: playerRace, gender: playerGender, pluginName: 'Skyrim.esm' },
    ...allSkyrimNpcs.filter((n) => !n.isChild),
  ]

  const searchLower = searchInput.trim().toLowerCase()
  const displayNpcs = searchLower.length === 0
    ? baseCandidates
    : baseCandidates.filter(
      (n) =>
        n.name.toLowerCase().includes(searchLower) ||
        n.race.toLowerCase().includes(searchLower) ||
        n.formId.toLowerCase().includes(searchLower) ||
        (n.pluginName && n.pluginName.toLowerCase().includes(searchLower)),
    )

  // Get current relationships for selected NPC
  const selectedRelSummary = selectedId
    ? relationships
      .filter((r) => r.actorA === selectedId || r.actorB === selectedId)
      .map((r) => {
        const partnerId = r.actorA === selectedId ? r.actorB : r.actorA
        const partnerName =
          partnerId === PLAYER_FORM_ID
            ? playerName
            : npcProfiles.get(partnerId)?.name || 'Unknown'
        return `${getStatusLabel(r.status)} with ${partnerName}`
      })
    : []

  const selectedNpc =
    selectedId === PLAYER_FORM_ID
      ? (npcProfiles.get(PLAYER_FORM_ID) || { name: playerName, race: playerRace, gender: playerGender, pluginName: 'Skyrim.esm' })
      : selectedId
        ? npcProfiles.get(selectedId) || allSkyrimNpcs.find((n) => n.formId === selectedId)
        : null

  const handleSelectNpc = (npc: CompactNPC | { formId: string; name: string; race: string; gender?: CompactNPC['gender']; pluginName?: string }) => {
    if (npc.formId === otherSelectedId) return
    const isSelected = selectedId === npc.formId
    if (isSelected) {
      setSelected(null)
    } else {
      if (npc.formId !== PLAYER_FORM_ID && !npcProfiles.has(npc.formId)) {
        updateNPCProfile(npc.formId, {
          formId: npc.formId,
          name: npc.name,
          race: npc.race,
          pluginName: npc.pluginName,
          gender: npc.gender ?? 'male',
        } as Partial<NPCProfile>)
      }
      setSelected(npc.formId)
    }
  }

  return (
    <div className="flex-1 bg-panel border border-border-subtle rounded-lg flex flex-col h-full overflow-hidden">
      {/* Header */}
      <div className="p-[0.75rem] border-b border-border-divider bg-panel-raised flex flex-col gap-[0.5rem]">
        <div className="text-xs font-semibold uppercase tracking-wider text-accent-bright flex items-center justify-between">
          <span>{title} ({displayNpcs.length})</span>
          <button
            onClick={() => fetchAllSkyrimNpcs()}
            disabled={isLoadingAllNpcs}
            title="Refresh Skyrim NPCs"
            className="flex items-center gap-[0.25rem] text-[0.6875rem] text-foreground-muted hover:text-accent p-[0.125rem] rounded transition-colors cursor-pointer normal-case font-normal"
          >
            <RefreshCw className={`w-[0.875em] h-[0.875em] ${isLoadingAllNpcs ? 'animate-spin text-accent' : ''}`} />
            <span>{isLoadingAllNpcs ? 'Refreshing...' : 'Refresh'}</span>
          </button>
        </div>
        <SearchInput
          value={searchInput}
          placeholder="Search any Skyrim NPC by name, race, plugin, or Form ID..."
          onChange={setSearchInput}
        />
      </div>

      {/* Selected Summary Header */}
      {selectedNpc && (
        <div className="p-[0.625rem] bg-accent-soft border-b border-border-subtle flex flex-col gap-[0.25rem]">
          <div className="text-xs font-bold text-accent-bright flex items-center justify-between">
            <div className="flex items-center gap-[0.25rem] flex-wrap">
              <Heart className="w-[0.875em] h-[0.875em] text-accent" />
              <span>Selected: {selectedNpc.name}</span>
              {selectedNpc.pluginName && (
                <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${selectedNpc.pluginName}`}>
                  {selectedNpc.pluginName}
                </span>
              )}
            </div>
            {'socialClass' in selectedNpc && (
              <NpcTraitBadges npc={selectedNpc as NPCProfile} size="sm" showIconOnly />
            )}
          </div>
          <div className="text-[0.6875rem] text-foreground-secondary">
            {selectedRelSummary.length > 0
              ? selectedRelSummary.join(', ')
              : 'No existing relationships'}
          </div>
        </div>
      )}

      {/* Virtualized NPC List */}
      <VirtualList
        items={displayNpcs}
        estimateSize={52}
        overscan={8}
        emptyMessage={isLoadingAllNpcs ? 'Loading Skyrim NPCs...' : 'No matching NPCs found.'}
        renderItem={(npc) => {
          const isSelected = selectedId === npc.formId
          const isDisabled = otherSelectedId === npc.formId
          const isPlayer = npc.formId === PLAYER_FORM_ID
          const profile = !isPlayer ? npcProfiles.get(npc.formId) : null
          const plugin = npc.pluginName || profile?.pluginName
          const isFemale = npc.gender === 'female'
          const genderLabel = isFemale ? '♀ Female' : '♂ Male'
          const genderColor = isFemale ? 'text-pink-400 bg-pink-950/40 border-pink-800/40' : 'text-blue-400 bg-blue-950/40 border-blue-800/40'

          return (
            <div
              onClick={() => handleSelectNpc(npc)}
              className={`flex items-center justify-between p-[0.5rem] rounded-md transition-all ${isDisabled
                ? 'opacity-40 bg-panel-inset border border-transparent cursor-not-allowed'
                : isSelected
                  ? 'bg-accent-soft border-l-2 border-l-accent border-t border-r border-b border-border-accent cursor-pointer'
                  : 'bg-panel-raised border border-border-subtle hover:border-border-strong hover:bg-interactive-hover cursor-pointer'
                }`}
            >
              <div className="flex items-center gap-[0.5rem] min-w-0">
                {isPlayer ? (
                  <Crown className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                ) : (
                  <User className="w-[1em] h-[1em] text-foreground-muted flex-shrink-0" />
                )}
                <div className="min-w-0">
                  <div className="flex items-center gap-[0.375rem]">
                    <span className="text-xs font-semibold text-foreground-primary truncate">
                      {npc.name}
                    </span>
                    <span className={`px-[0.375rem] py-[0.0625rem] rounded-full text-[0.625rem] border font-normal flex-shrink-0 ${genderColor}`}>
                      {genderLabel}
                    </span>
                  </div>
                  <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.375rem] flex-wrap">
                    <span>{npc.race}</span>
                    <span>•</span>
                    <span className="font-mono">{npc.formId}</span>
                    {plugin && (
                      <>
                        <span>•</span>
                        <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[6.5rem]" title={`Plugin: ${plugin}`}>
                          {plugin}
                        </span>
                      </>
                    )}
                    {profile && <NpcTraitBadges npc={profile} size="sm" showIconOnly />}
                  </div>
                </div>
              </div>
            </div>
          )
        }}
      />
    </div>
  )
}
