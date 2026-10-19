import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { Heart, Crown, HeartOff, UserCheck, Flame } from 'lucide-react'
export const StatCards: React.FC = () => {
  const relationships = useMarasStore((s) => s.relationships)

  const getCount = (status: RelationshipStatus) => {
    return relationships.filter(
      (r) =>
        r.status === status && (r.actorA === PLAYER_FORM_ID || r.actorB === PLAYER_FORM_ID),
    ).length
  }

  const stats = [
    { label: 'Married', count: getCount(RelationshipStatus.Married), icon: Crown, color: 'text-accent-bright' },
    { label: 'Dating', count: getCount(RelationshipStatus.Dating), icon: Heart, color: 'text-status-success' },
    { label: 'Lovers', count: getCount(RelationshipStatus.Lover), icon: Flame, color: 'text-accent' },
    { label: 'Engaged', count: getCount(RelationshipStatus.Engaged), icon: UserCheck, color: 'text-status-warning' },
    { label: 'Divorced', count: getCount(RelationshipStatus.Divorced), icon: HeartOff, color: 'text-status-danger' },
  ]

  return (
    <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-5 gap-[0.75rem]">
      {stats.map((stat, i) => {
        const Icon = stat.icon
        return (
          <div
            key={i}
            className="bg-panel-raised border border-border-subtle hover:border-border-strong rounded-md p-[0.75rem] flex flex-col justify-between transition-colors"
          >
            <div className="flex items-center justify-between text-foreground-muted mb-[0.25rem]">
              <span className="text-xs font-medium uppercase tracking-wider">{stat.label}</span>
              <Icon className={`w-[1.125em] h-[1.125em] ${stat.color}`} />
            </div>
            <div className={`text-2xl font-bold font-mono ${stat.color}`}>{stat.count}</div>
          </div>
        )
      })}
    </div>
  )
}
