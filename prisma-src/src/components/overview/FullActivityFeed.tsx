import React, { useState, useMemo } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { getStatusLabel, StatusBadge, StatusIcon } from '../shared/StatusBadge'
import { formatGameDay } from '../../utils/dateUtils'
import { ScrollArea } from '@/components/ui/scroll-area'
import {
  Activity,
  Search,
  ArrowLeft,
  Clock,
  Calendar,
  Filter,
  X,
  Heart,
  Sparkles,
} from 'lucide-react'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { RelationshipStatus } from '../../types'

type FilterCategory = 'all' | 'union' | 'romance' | 'ended'

export const FullActivityFeed: React.FC = () => {
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)

  const [searchTerm, setSearchTerm] = useState('')
  const [selectedCategory, setSelectedCategory] = useState<FilterCategory>('all')

  // Flatten and sort all relationship status events
  const allEvents = useMemo(() => {
    const eventsList: Array<{
      id: string
      actorA: string
      actorB: string
      fromStatus: number
      toStatus: number
      timestamp: number
      orderIndex: number
    }> = []

    relationships.forEach((r, pairIdx) => {
      r.statusHistory.forEach((h, histIdx) => {
        eventsList.push({
          id: `${pairIdx}-${histIdx}-${h.timestamp}`,
          actorA: r.actorA,
          actorB: r.actorB,
          fromStatus: h.fromStatus,
          toStatus: h.toStatus,
          timestamp: h.timestamp,
          orderIndex: histIdx,
        })
      })
    })

    // Sort newest timestamp first, falling back to chronological history order index
    eventsList.sort((a, b) => {
      if (Math.abs(b.timestamp - a.timestamp) > 0.0001) {
        return b.timestamp - a.timestamp
      }
      return b.orderIndex - a.orderIndex
    })
    return eventsList
  }, [relationships])

  const getName = (id: string) => {
    if (id === PLAYER_FORM_ID) return npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
    return npcProfiles.get(id)?.name || `NPC ${id}`
  }

  // Filter events based on search term & category selection
  const filteredEvents = useMemo(() => {
    return allEvents.filter((ev) => {
      const nameA = getName(ev.actorA).toLowerCase()
      const nameB = getName(ev.actorB).toLowerCase()
      const searchMatch =
        !searchTerm.trim() ||
        nameA.includes(searchTerm.toLowerCase()) ||
        nameB.includes(searchTerm.toLowerCase()) ||
        getStatusLabel(ev.toStatus).toLowerCase().includes(searchTerm.toLowerCase())

      if (!searchMatch) return false

      if (selectedCategory === 'union') {
        return (
          ev.toStatus === RelationshipStatus.Married ||
          ev.toStatus === RelationshipStatus.Engaged
        )
      }
      if (selectedCategory === 'romance') {
        return (
          ev.toStatus === RelationshipStatus.Dating ||
          ev.toStatus === RelationshipStatus.Lover
        )
      }
      if (selectedCategory === 'ended') {
        return (
          ev.toStatus === RelationshipStatus.Divorced ||
          ev.toStatus === RelationshipStatus.Jilted ||
          ev.toStatus === RelationshipStatus.ExDating ||
          ev.toStatus === RelationshipStatus.ExLover
        )
      }

      return true
    })
  }, [allEvents, searchTerm, selectedCategory, npcProfiles])

  // Count stats
  const unionCount = useMemo(
    () =>
      allEvents.filter(
        (e) =>
          e.toStatus === RelationshipStatus.Married || e.toStatus === RelationshipStatus.Engaged,
      ).length,
    [allEvents],
  )
  const romanceCount = useMemo(
    () =>
      allEvents.filter(
        (e) =>
          e.toStatus === RelationshipStatus.Dating || e.toStatus === RelationshipStatus.Lover,
      ).length,
    [allEvents],
  )
  const endedCount = useMemo(
    () =>
      allEvents.filter(
        (e) =>
          e.toStatus === RelationshipStatus.Divorced ||
          e.toStatus === RelationshipStatus.Jilted ||
          e.toStatus === RelationshipStatus.ExDating ||
          e.toStatus === RelationshipStatus.ExLover,
      ).length,
    [allEvents],
  )

  const renderActorName = (actorId: string) => {
    const name = getName(actorId)
    return (
      <span
        role="button"
        tabIndex={0}
        onClick={(e) => {
          e.stopPropagation()
          openNpcDetail(actorId)
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            openNpcDetail(actorId)
          }
        }}
        className="font-semibold text-foreground-primary hover:text-accent-bright hover:underline cursor-pointer transition-colors"
      >
        {name}
      </span>
    )
  }

  return (
    <div className="flex flex-col gap-[1rem] h-full w-full">
      {/* Header Bar */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-[0.75rem] bg-panel border border-border-subtle rounded-lg p-[1rem]">
        <div className="flex items-center gap-[0.75rem]">
          <button
            onClick={() => setActiveSubSection('dashboard')}
            className="p-[0.5rem] rounded-md bg-panel-raised border border-border-subtle text-foreground-muted hover:text-accent-bright hover:bg-accent-soft transition-colors cursor-pointer"
            title="Back to Dashboard"
          >
            <ArrowLeft className="w-[1.125em] h-[1.125em]" />
          </button>
          <div>
            <h2 className="text-lg font-bold text-foreground-primary flex items-center gap-[0.5rem]">
              <Activity className="w-[1.25em] h-[1.25em] text-accent" />
              Full Activity Feed & Relationship History
            </h2>
            <p className="text-xs text-foreground-muted">
              Chronological ledger of relationship milestone transitions across Skyrim
            </p>
          </div>
        </div>

        <div className="flex items-center gap-[0.5rem]">
          <span className="text-xs font-semibold px-[0.625rem] py-[0.25rem] bg-accent-soft text-accent-bright rounded-full border border-accent/20">
            {allEvents.length} Total Events
          </span>
        </div>
      </div>

      {/* Filter & Search Toolbar */}
      <div className="bg-panel border border-border-subtle rounded-lg p-[0.75rem] flex flex-col md:flex-row items-center justify-between gap-[0.75rem]">
        {/* Category Filters */}
        <div className="flex items-center gap-[0.375rem] flex-wrap w-full md:w-auto">
          <span className="text-xs text-foreground-muted flex items-center gap-[0.25rem] mr-[0.25rem]">
            <Filter className="w-[0.875em] h-[0.875em]" /> Filter:
          </span>

          <button
            onClick={() => setSelectedCategory('all')}
            className={`px-[0.625rem] py-[0.3125rem] rounded-md text-xs font-medium transition-colors cursor-pointer ${selectedCategory === 'all'
              ? 'bg-accent text-white font-semibold shadow-sm'
              : 'bg-panel-raised text-foreground-secondary hover:bg-interactive-hover'
              }`}
          >
            All ({allEvents.length})
          </button>

          <button
            onClick={() => setSelectedCategory('union')}
            className={`px-[0.625rem] py-[0.3125rem] rounded-md text-xs font-medium transition-colors cursor-pointer flex items-center gap-[0.25rem] ${selectedCategory === 'union'
              ? 'bg-accent text-white font-semibold shadow-sm'
              : 'bg-panel-raised text-foreground-secondary hover:bg-interactive-hover'
              }`}
          >
            <StatusIcon status={RelationshipStatus.Engaged} /> Marriages & Engagements ({unionCount})
          </button>

          <button
            onClick={() => setSelectedCategory('romance')}
            className={`px-[0.625rem] py-[0.3125rem] rounded-md text-xs font-medium transition-colors cursor-pointer flex items-center gap-[0.25rem] ${selectedCategory === 'romance'
              ? 'bg-accent text-white font-semibold shadow-sm'
              : 'bg-panel-raised text-foreground-secondary hover:bg-interactive-hover'
              }`}
          >
            <StatusIcon status={RelationshipStatus.Lover} /> Dating & Lovers ({romanceCount})
          </button>

          <button
            onClick={() => setSelectedCategory('ended')}
            className={`px-[0.625rem] py-[0.3125rem] rounded-md text-xs font-medium transition-colors cursor-pointer flex items-center gap-[0.25rem] ${selectedCategory === 'ended'
              ? 'bg-accent text-white font-semibold shadow-sm'
              : 'bg-panel-raised text-foreground-secondary hover:bg-interactive-hover'
              }`}
          >
            <StatusIcon status={RelationshipStatus.ExLover} /> Divorced & Ended ({endedCount})
          </button>
        </div>

        {/* Search input */}
        <div className="relative w-full md:w-[12rem]">
          <Search className="w-[1em] h-[1em] absolute left-[0.625rem] top-[50%] translate-y-[-50%] text-foreground-muted pointer-events-none" />
          <input
            type="text"
            value={searchTerm}
            onChange={(e) => setSearchTerm(e.target.value)}
            placeholder="Search by actor or status..."
            className="w-full bg-panel-inset border border-border-subtle rounded-md pl-[2rem] pr-[2rem] py-[0.375rem] text-xs text-foreground-primary placeholder:text-foreground-muted focus:outline-none focus:border-accent"
          />
          {searchTerm && (
            <button
              onClick={() => setSearchTerm('')}
              className="absolute right-[0.5rem] top-[50%] translate-y-[-50%] text-foreground-muted hover:text-foreground-primary"
            >
              <X className="w-[0.875em] h-[0.875em]" />
            </button>
          )}
        </div>
      </div>

      {/* Main Activity Events Scroll View */}
      <div className="bg-panel border border-border-subtle rounded-lg p-[1rem] flex-1 flex flex-col overflow-hidden min-h-[22rem]">
        <ScrollArea className="h-full pr-[0.75rem]">
          {filteredEvents.length === 0 ? (
            <div className="flex flex-col items-center justify-center py-[4rem] gap-[0.75rem] text-center">
              <Activity className="w-[2.5rem] h-[2.5rem] text-foreground-muted opacity-40" />
              <p className="text-sm text-foreground-muted font-medium">
                {searchTerm || selectedCategory !== 'all'
                  ? 'No relationship activity matching your current filter'
                  : 'No relationship activity history recorded yet'}
              </p>
              {(searchTerm || selectedCategory !== 'all') && (
                <button
                  onClick={() => {
                    setSearchTerm('')
                    setSelectedCategory('all')
                  }}
                  className="text-xs text-accent hover:underline flex items-center gap-[0.25rem]"
                >
                  Reset filters
                </button>
              )}
            </div>
          ) : (
            <div className="space-y-[0.625rem]">
              {filteredEvents.map((ev) => {
                const fromLabel = getStatusLabel(ev.fromStatus)
                const toLabel = getStatusLabel(ev.toStatus)

                return (
                  <div
                    key={ev.id}
                    className="flex flex-col sm:flex-row sm:items-center justify-between gap-[0.75rem] p-[0.75rem] bg-panel-raised border-l-4 border-l-accent border border-border-subtle rounded-r-md text-xs hover:border-accent/40 transition-colors"
                  >
                    <div className="flex items-start sm:items-center gap-[0.75rem]">
                      <span className="p-[0.375rem] bg-panel-inset rounded-md border border-border-subtle flex-shrink-0 flex items-center justify-center">
                        <StatusIcon status={ev.toStatus} size="1.5em" />
                      </span>
                      <div className="flex flex-col gap-[0.25rem]">
                        <div className="flex items-center gap-[0.375rem] flex-wrap text-sm">
                          {renderActorName(ev.actorA)}
                          <span className="text-foreground-secondary font-semibold">&</span>
                          {renderActorName(ev.actorB)}
                        </div>

                        <div className="flex items-center gap-[0.375rem] text-foreground-muted text-xs">
                          {ev.timestamp < 0 ? (
                            <span className="bg-panel-inset px-[0.375rem] py-[0.0625rem] rounded text-[0.6875rem] text-foreground-secondary">
                              Pre-existing
                            </span>
                          ) : (
                            <span className="bg-panel-inset px-[0.375rem] py-[0.0625rem] rounded text-[0.6875rem]">
                              {fromLabel}
                            </span>
                          )}
                          <span>→</span>
                          <StatusBadge status={ev.toStatus} />
                        </div>
                      </div>
                    </div>

                    <div className="flex items-center gap-[0.75rem] text-foreground-muted text-[0.75rem] self-end sm:self-center">
                      <div className="flex items-center gap-[0.25rem]">
                        <Clock className="w-[0.875em] h-[0.875em] text-accent" />
                        <span>{formatGameDay(ev.timestamp, currentGameDay)}</span>
                      </div>
                      <div className="flex items-center gap-[0.25rem] bg-panel-inset px-[0.5rem] py-[0.125rem] rounded border border-border-subtle">
                        <Calendar className="w-[0.75em] h-[0.75em]" />
                        <span>{ev.timestamp < 0 ? 'Historical' : `Day ${Math.floor(ev.timestamp)}`}</span>
                      </div>
                    </div>
                  </div>
                )
              })}
            </div>
          )}
        </ScrollArea>
      </div>
    </div>
  )
}
