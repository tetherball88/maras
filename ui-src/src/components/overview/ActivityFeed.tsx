import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { getStatusLabel, StatusIcon } from '../shared/StatusBadge'
import { formatGameDay } from '../../utils/dateUtils'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Activity, Clock, ArrowRight } from 'lucide-react'
import { PLAYER_FORM_ID } from '../../data/mockData'

interface ActivityFeedProps {
  limit?: number
}

export const ActivityFeed: React.FC<ActivityFeedProps> = ({ limit = 5 }) => {
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)

  // Flatten status histories across all relationships
  const events: Array<{
    actorA: string
    actorB: string
    fromStatus: number
    toStatus: number
    timestamp: number
    orderIndex: number
  }> = []

  relationships.forEach((r) => {
    r.statusHistory.forEach((h, histIdx) => {
      events.push({
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
  events.sort((a, b) => {
    if (Math.abs(b.timestamp - a.timestamp) > 0.0001) {
      return b.timestamp - a.timestamp
    }
    return b.orderIndex - a.orderIndex
  })

  const getName = (id: string) => {
    if (id === PLAYER_FORM_ID) return npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
    return npcProfiles.get(id)?.name || 'Unknown NPC'
  }

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
    <div className="bg-panel border border-border-subtle rounded-lg p-[1rem] flex flex-col gap-[0.75rem] h-full">
      <div className="flex items-center justify-between border-b border-border-divider pb-[0.5rem]">
        <h3 className="text-sm font-semibold text-accent-bright flex items-center gap-[0.375rem]">
          <Activity className="w-[1.125em] h-[1.125em] text-accent" /> Recent Activity Feed
        </h3>
        <div className="flex items-center gap-[0.75rem]">
          <span className="text-xs text-foreground-muted">{events.length} Events</span>
          <button
            onClick={() => setActiveSubSection('activity')}
            className="text-xs text-accent hover:text-accent-bright hover:underline flex items-center gap-[0.25rem] transition-colors cursor-pointer font-medium"
          >
            View Full Feed <ArrowRight className="w-[0.875em] h-[0.875em]" />
          </button>
        </div>
      </div>

      <ScrollArea className="h-[16rem] pr-[0.5rem]">
        <div className="space-y-[0.5rem]">
          {events.length === 0 ? (
            <div className="text-xs text-foreground-muted text-center py-[2rem]">No relationship history recorded</div>
          ) : (
            events.slice(0, limit).map((ev, idx) => {
              const toLabel = getStatusLabel(ev.toStatus)

              return (
                <div
                  key={idx}
                  className="flex items-center gap-[0.625rem] p-[0.5rem] bg-panel-raised border-l-2 border-l-accent border border-border-subtle rounded-r-md text-xs"
                >
                  <StatusIcon status={ev.toStatus} size="1.25em" />
                  <div className="flex-1">
                    {renderActorName(ev.actorA)}
                    <span className="text-foreground-secondary"> & </span>
                    {renderActorName(ev.actorB)}
                    <span className="text-foreground-muted"> → </span>
                    <span className="text-accent font-medium">{toLabel}</span>
                  </div>
                  <div className="flex items-center gap-[0.25rem] text-[0.6875rem] text-foreground-muted flex-shrink-0">
                    <Clock className="w-[0.875em] h-[0.875em]" />
                    <span>{formatGameDay(ev.timestamp, currentGameDay)}</span>
                  </div>
                </div>
              )
            })
          )}
        </div>
      </ScrollArea>
    </div>
  )
}

