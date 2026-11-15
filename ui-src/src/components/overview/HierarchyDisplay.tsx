import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { AffectionBar } from '../shared/AffectionBar'
import { StatusBadge } from '../shared/StatusBadge'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { RelationshipStatus } from '../../types'
import { Crown, Medal, Award } from 'lucide-react'

export const HierarchyDisplay: React.FC = () => {
  const hierarchyRanks = useMarasStore((s) => s.hierarchyRanks)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const affectionData = useMarasStore((s) => s.affectionData)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)

  const rankBadges = [
    { rank: '1st Spouse', icon: Crown, color: 'text-amber-400 border-amber-500/30' },
    { rank: '2nd Spouse', icon: Medal, color: 'text-slate-300 border-slate-400/30' },
    { rank: '3rd Spouse', icon: Award, color: 'text-amber-600 border-amber-700/30' },
  ]

  const handleCardClick = (formId: string) => {
    if (formId) {
      openNpcDetail(formId)
    }
  }

  return (
    <div className="flex flex-col gap-[0.75rem]">
      <div className="flex items-center justify-between">
        <h3 className="text-base font-semibold text-accent-bright flex items-center gap-[0.375rem]">
          <Crown className="w-[1.125em] h-[1.125em] text-accent" /> Spouse Hierarchy (Top 3 Ranks)
        </h3>
        <span className="text-xs text-foreground-muted">Managed via Marriage & Spouse Assets</span>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-[0.75rem]">
        {rankBadges.map((badge, idx) => {
          const formId = hierarchyRanks[idx]
          const npc = formId ? npcProfiles.get(formId) : null
          const affection = formId ? affectionData.get(formId)?.permanent ?? 0 : 0
          const Icon = badge.icon

          return (
            <div
              key={idx}
              onClick={() => formId && handleCardClick(formId)}
              className={`bg-panel-raised border rounded-md p-[0.875rem] flex flex-col justify-between transition-all ${formId
                ? 'border-border-strong hover:border-border-accent cursor-pointer hover:shadow-raised'
                : 'border-border-divider border-dashed opacity-60'
                }`}
            >
              <div className="flex items-center justify-between mb-[0.5rem]">
                <span className={`text-xs font-semibold px-[0.5rem] py-[0.125rem] rounded-full border bg-panel flex items-center gap-[0.25rem] ${badge.color}`}>
                  <Icon className="w-[1em] h-[1em]" /> {badge.rank}
                </span>
                {formId && <StatusBadge status={RelationshipStatus.Married} />}
              </div>

              {npc ? (
                <div className="space-y-[0.5rem]">
                  <div>
                    <div className="text-base font-semibold text-foreground-primary">{npc.name}</div>
                    <div className="text-xs text-foreground-secondary mb-[0.25rem] flex items-center gap-[0.25rem] flex-wrap">
                      <span>{npc.race}</span>
                      {npc.pluginName && (
                        <>
                          <span>•</span>
                          <span className="font-mono text-[0.625rem] text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${npc.pluginName}`}>
                            {npc.pluginName}
                          </span>
                        </>
                      )}
                      <span>•</span>
                      <span>{npc.currentHome || 'No Home Set'}</span>
                    </div>
                    <NpcTraitBadges npc={npc} size="md" />
                  </div>
                  <AffectionBar value={affection} size="sm" />
                </div>
              ) : (
                <div className="py-[1rem] text-center text-xs text-foreground-muted italic">
                  Unassigned Rank Slot
                </div>
              )}
            </div>
          )
        })}
      </div>
    </div>
  )
}
