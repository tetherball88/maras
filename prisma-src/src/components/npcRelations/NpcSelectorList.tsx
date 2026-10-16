import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { SearchInput } from '../shared/SearchInput'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { VirtualList } from '../shared/VirtualList'
import { Crown, User } from 'lucide-react'

export const NpcSelectorList: React.FC = () => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const selectedId = useMarasStore((s) => s.selectedNpcRelationsId)
  const setSelectedId = useMarasStore((s) => s.setSelectedNpcRelationsId)

  const [search, setSearch] = useState('')

  // Count relationships per actor
  const getPartnerCount = (formId: string) => {
    return relationships.filter((r) => r.actorA === formId || r.actorB === formId).length
  }

  // List of all registered NPCs including player
  const playerItem = {
    formId: PLAYER_FORM_ID,
    name: npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)',
    race: npcProfiles.get(PLAYER_FORM_ID)?.race || 'Nord',
    gender: npcProfiles.get(PLAYER_FORM_ID)?.gender || 'female',
    pluginName: 'Skyrim.esm',
  }
  const allNpcs = [
    playerItem,
    ...Array.from(npcProfiles.values()).filter((n) => n.formId !== PLAYER_FORM_ID),
  ]

  const filtered = allNpcs.filter(
    (npc) =>
      npc.name.toLowerCase().includes(search.toLowerCase()) ||
      npc.race.toLowerCase().includes(search.toLowerCase()) ||
      npc.formId.toLowerCase().includes(search.toLowerCase()) ||
      (npc.pluginName && npc.pluginName.toLowerCase().includes(search.toLowerCase())),
  )

  return (
    <div className="w-[16rem] flex-shrink-0 bg-panel border border-border-subtle rounded-lg flex flex-col h-full overflow-hidden">
      {/* Search Input Header */}
      <div className="p-[0.75rem] border-b border-border-divider bg-panel-raised">
        <SearchInput
          value={search}
          placeholder="Search name, race, plugin, or Form ID..."
          onChange={setSearch}
        />
      </div>

      {/* Virtualized Scrollable List */}
      <VirtualList
        items={filtered}
        estimateSize={52}
        overscan={6}
        className="flex-1 min-h-0 p-[0.5rem] overflow-y-auto"
        emptyMessage="No characters found."
        renderItem={(npc) => {
          const isSelected = selectedId === npc.formId
          const count = getPartnerCount(npc.formId)
          const isPlayer = npc.formId === PLAYER_FORM_ID
          const profile = !isPlayer ? npcProfiles.get(npc.formId) : null
          const plugin = npc.pluginName || profile?.pluginName

          return (
            <div
              onClick={() => setSelectedId(npc.formId)}
              className={`flex items-center justify-between p-[0.5rem] rounded-md transition-all cursor-pointer ${isSelected
                ? 'bg-accent-soft border-l-2 border-l-accent border-t border-r border-b border-border-accent'
                : 'bg-panel-raised border border-border-subtle hover:border-border-strong hover:bg-interactive-hover'
                }`}
            >
              <div className="flex items-center gap-[0.5rem] min-w-0 flex-1">
                {isPlayer ? (
                  <Crown className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                ) : (
                  <User className="w-[1em] h-[1em] text-foreground-muted flex-shrink-0" />
                )}
                <div className="min-w-0">
                  <div className="text-xs font-semibold text-foreground-primary truncate">
                    {npc.name}
                  </div>
                  <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.375rem] flex-wrap">
                    <span className="font-mono text-accent-bright/80">{npc.formId}</span>
                    <span>•</span>
                    <span>{npc.race}</span>
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

              <span className="text-[0.6875rem] font-mono font-semibold px-[0.375rem] py-[0.125rem] rounded bg-panel-inset text-accent-bright border border-border-subtle">
                {count}
              </span>
            </div>
          )
        }}
      />
    </div>
  )
}
