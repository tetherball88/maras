import React from 'react'
import { NPCProfile, RelationshipStatus } from '../../types'
import { AffectionBar } from '../shared/AffectionBar'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { AppTooltip } from '../shared/AppTooltip'
import { ChevronRight, Trash2 } from 'lucide-react'

interface PartnerListItemProps {
  npc: NPCProfile
  status: RelationshipStatus
  affection: number
  isSelected: boolean
  onClick: () => void
  onRemove?: (e: React.MouseEvent) => void
}

export const PartnerListItem: React.FC<PartnerListItemProps> = ({
  npc,
  status,
  affection,
  isSelected,
  onClick,
  onRemove,
}) => {
  const hasAffection =
    status === RelationshipStatus.Married ||
    status === RelationshipStatus.Engaged ||
    status === RelationshipStatus.Dating ||
    status === RelationshipStatus.Lover

  return (
    <div
      onClick={onClick}
      className={`flex items-center justify-between p-[0.625rem] rounded-md transition-all cursor-pointer ${isSelected
        ? 'bg-accent-soft border-l-2 border-l-accent border-t border-r border-b border-border-accent'
        : 'bg-panel-raised border border-border-subtle hover:border-border-strong hover:bg-interactive-hover'
        }`}
    >
      <div className="flex-1 min-w-0 pr-[0.5rem]">
        <div className="flex items-center gap-[0.375rem] flex-wrap">
          <span className="text-sm font-semibold text-foreground-primary truncate">{npc.name}</span>
          <span className="text-xs font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle">{npc.formId}</span>
          <span className="text-xs text-foreground-muted">({npc.race})</span>
          {npc.pluginName && (
            <span className="text-[0.6875rem] font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[8rem]" title={`Plugin: ${npc.pluginName}`}>
              {npc.pluginName}
            </span>
          )}
          <NpcTraitBadges npc={npc} size="sm" showIconOnly />
        </div>
        <div className="text-xs text-foreground-secondary truncate mt-[0.125rem]">
          {npc.currentHome || npc.originalHome || 'No Home Set'}
        </div>
      </div>

      <div className="w-[8.5rem] flex-shrink-0 flex items-center justify-end gap-[0.375rem]">
        {hasAffection && <AffectionBar value={affection} size="sm" />}
        {onRemove && status !== RelationshipStatus.Candidate && (
          <AppTooltip content="Remove Relationship Record (Reset to Candidate)">
            <button
              onClick={(e) => {
                e.stopPropagation()
                onRemove(e)
              }}
              className="p-[0.25rem] rounded text-foreground-muted hover:text-status-danger hover:bg-rose-950/40 transition-colors cursor-pointer"
            >
              <Trash2 className="w-[0.95em] h-[0.95em]" />
            </button>
          </AppTooltip>
        )}
        <ChevronRight className={`w-[1em] h-[1em] ${isSelected ? 'text-accent' : 'text-foreground-muted'}`} />
      </div>
    </div>
  )
}
