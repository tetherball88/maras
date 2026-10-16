import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge } from '../shared/StatusBadge'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { SearchInput } from '../shared/SearchInput'
import { SelectInput } from '../shared/SelectInput'
import { formatGameDay } from '../../utils/dateUtils'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Sparkles, Users, Crown, User, Clock, Heart } from 'lucide-react'

export const NpcAllPairsList: React.FC = () => {
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)

  const [search, setSearch] = useState('')
  const [statusFilter, setStatusFilter] = useState<string>('all')

  const getActorName = (id: string) => {
    if (id === PLAYER_FORM_ID) return npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
    return npcProfiles.get(id)?.name || `NPC (${id.slice(-6)})`
  }

  // Filter pairs by search query and status filter
  const filteredPairs = relationships.filter((rel) => {
    // Status filter
    if (statusFilter !== 'all') {
      const numericStatus = parseInt(statusFilter, 10)
      if (rel.status !== numericStatus) return false
    }

    // Search filter (matches actorA or actorB name or form ID)
    if (search.trim()) {
      const q = search.toLowerCase()
      const nameA = getActorName(rel.actorA).toLowerCase()
      const nameB = getActorName(rel.actorB).toLowerCase()
      const matchA = nameA.includes(q) || rel.actorA.toLowerCase().includes(q)
      const matchB = nameB.includes(q) || rel.actorB.toLowerCase().includes(q)
      if (!matchA && !matchB) return false
    }

    return true
  })

  const statusOptions = [
    { value: 'all', label: 'All Statuses' },
    ...Object.keys(RelationshipStatus)
      .filter((k) => isNaN(Number(k)))
      .map((key, idx) => ({
        value: idx.toString(),
        label: key,
      })),
  ]

  return (
    <div className="flex-1 flex flex-col h-full overflow-hidden bg-panel border border-border-subtle rounded-lg p-[1rem] gap-[1rem]">
      {/* Header with Title and Search/Filter Controls */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-[0.75rem] border-b border-border-divider pb-[0.75rem]">
        <div className="flex items-center gap-[0.5rem]">
          <Sparkles className="w-[1.25em] h-[1.25em] text-accent" />
          <h2 className="text-base font-bold text-accent-bright">All NPC Relationship Pairs</h2>
          <span className="text-xs px-[0.5rem] py-[0.125rem] rounded bg-panel-inset text-foreground-muted border border-border-subtle font-mono">
            {filteredPairs.length} / {relationships.length} Pairs
          </span>
        </div>

        <div className="flex items-center gap-[0.75rem] w-full sm:w-auto">
          <SearchInput
            value={search}
            placeholder="Search by NPC name..."
            onChange={setSearch}
            className="w-full sm:w-[13rem]"
          />
          <SelectInput
            value={statusFilter}
            options={statusOptions}
            onChange={setStatusFilter}
            className="w-full sm:w-[10rem]"
          />
        </div>
      </div>

      {/* Grid of Relationship Cards */}
      <ScrollArea className="flex-1 min-h-0 pr-[0.5rem]">
        {filteredPairs.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-[4rem] text-center gap-[0.5rem]">
            <Users className="w-[2em] h-[2em] text-foreground-muted" />
            <div className="text-sm font-semibold text-foreground-secondary">No relationship pairs found</div>
            <div className="text-xs text-foreground-muted">Try adjusting your search query or status filter.</div>
          </div>
        ) : (
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-[0.75rem]">
            {filteredPairs.map((pair, idx) => {
              const profileA = pair.actorA !== PLAYER_FORM_ID ? npcProfiles.get(pair.actorA) : null
              const profileB = pair.actorB !== PLAYER_FORM_ID ? npcProfiles.get(pair.actorB) : null

              const nameA = getActorName(pair.actorA)
              const nameB = getActorName(pair.actorB)

              // Determine relevant date if available
              const dateVal =
                pair.statusHistory && pair.statusHistory.length > 0
                  ? pair.statusHistory[pair.statusHistory.length - 1].timestamp
                  : null

              return (
                <div
                  key={idx}
                  className="bg-panel-raised border border-border-subtle hover:border-border-accent rounded-lg p-[0.875rem] flex flex-col justify-between gap-[0.75rem] transition-all hover:shadow-raised"
                >
                  {/* Card Header: Status Badge & Date */}
                  <div className="flex items-center justify-between border-b border-border-divider pb-[0.5rem]">
                    <StatusBadge status={pair.status} />
                    {dateVal != null && (
                      <div className="flex items-center gap-[0.25rem] text-[0.6875rem] text-foreground-muted">
                        <Clock className="w-[0.875em] h-[0.875em]" />
                        <span>{formatGameDay(dateVal, currentGameDay)}</span>
                      </div>
                    )}
                  </div>

                  {/* Card Body: Pair Actors */}
                  <div className="flex items-center justify-between gap-[0.5rem]">
                    {/* Actor A */}
                    <div
                      onClick={() => openNpcDetail(pair.actorA)}
                      className="flex-1 min-w-0 p-[0.5rem] bg-panel-inset rounded border border-border-divider transition-all cursor-pointer hover:border-border-accent hover:bg-interactive-hover"
                    >
                      <div className="flex items-center gap-[0.375rem] mb-[0.125rem]">
                        {pair.actorA === PLAYER_FORM_ID ? (
                          <Crown className="w-[0.875em] h-[0.875em] text-accent flex-shrink-0" />
                        ) : (
                          <User className="w-[0.875em] h-[0.875em] text-foreground-muted flex-shrink-0" />
                        )}
                        <span className="text-xs font-semibold text-foreground-primary truncate hover:text-accent-bright">
                          {nameA}
                        </span>
                      </div>
                      {profileA ? (
                        <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem] flex-wrap">
                          {profileA.pluginName && (
                            <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[5.5rem]" title={`Plugin: ${profileA.pluginName}`}>
                              {profileA.pluginName}
                            </span>
                          )}
                          <NpcTraitBadges npc={profileA} size="sm" showIconOnly />
                        </div>
                      ) : (
                        <div className="text-[0.6875rem] text-foreground-muted">
                          {pair.actorA === PLAYER_FORM_ID ? 'Protagonist' : 'NPC Profile'}
                        </div>
                      )}
                    </div>

                    {/* Connector Icon */}
                    <Heart className="w-[0.875em] h-[0.875em] text-accent flex-shrink-0" />

                    {/* Actor B */}
                    <div
                      onClick={() => openNpcDetail(pair.actorB)}
                      className="flex-1 min-w-0 p-[0.5rem] bg-panel-inset rounded border border-border-divider transition-all cursor-pointer hover:border-border-accent hover:bg-interactive-hover"
                    >
                      <div className="flex items-center gap-[0.375rem] mb-[0.125rem]">
                        {pair.actorB === PLAYER_FORM_ID ? (
                          <Crown className="w-[0.875em] h-[0.875em] text-accent flex-shrink-0" />
                        ) : (
                          <User className="w-[0.875em] h-[0.875em] text-foreground-muted flex-shrink-0" />
                        )}
                        <span className="text-xs font-semibold text-foreground-primary truncate hover:text-accent-bright">
                          {nameB}
                        </span>
                      </div>
                      {profileB ? (
                        <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem] flex-wrap">
                          {profileB.pluginName && (
                            <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[5.5rem]" title={`Plugin: ${profileB.pluginName}`}>
                              {profileB.pluginName}
                            </span>
                          )}
                          <NpcTraitBadges npc={profileB} size="sm" showIconOnly />
                        </div>
                      ) : (
                        <div className="text-[0.6875rem] text-foreground-muted">
                          {pair.actorB === PLAYER_FORM_ID ? 'Protagonist' : 'NPC Profile'}
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              )
            })}
          </div>
        )}
      </ScrollArea>
    </div>
  )
}
