import React, { useState } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { RelationshipStatus } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge, getStatusLabel, StatusIcon } from '../shared/StatusBadge'
import { Button } from '../ui/button'
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from '@/components/ui/tooltip'
import { ScrollArea } from '@/components/ui/scroll-area'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { HeartHandshake, ArrowRightLeft, AlertCircle, Check, ShieldOff, ShieldCheck } from 'lucide-react'
import { useAvailableStatuses } from '../../utils/statusAvailability'

export const StatusAssignment: React.FC = () => {
  const npcA = useMarasStore((s) => s.matchmakingNpcA)
  const npcB = useMarasStore((s) => s.matchmakingNpcB)
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const assignRelationship = useMarasStore((s) => s.assignRelationshipStatus)
  const removeRelationship = useMarasStore((s) => s.removeRelationship)
  const bypassRules = useMarasStore((s) => s.bypassStatusRules)
  const setBypassRules = useMarasStore((s) => s.setBypassStatusRules)

  const [selectedTargetStatus, setSelectedTargetStatus] = useState<RelationshipStatus | null>(null)
  const [successFeedback, setSuccessFeedback] = useState(false)

  const statusOptions = useAvailableStatuses(npcA, npcB)

  if (!npcA || !npcB) {
    return (
      <div className="w-[18rem] flex-shrink-0 bg-panel border border-border-subtle rounded-lg flex items-center justify-center p-[1.5rem] text-center text-foreground-muted">
        <div className="flex flex-col items-center gap-[0.75rem]">
          <HeartHandshake className="w-[2em] h-[2em] text-accent" />
          <span className="text-xs">
            Select <strong>NPC A</strong> on the left and <strong>NPC B</strong> on the right to configure their relationship status.
          </span>
        </div>
      </div>
    )
  }

  // Find existing pair relationship
  const existingRel = relationships.find(
    (r) =>
      (r.actorA === npcA && r.actorB === npcB) ||
      (r.actorA === npcB && r.actorB === npcA),
  )

  const currentStatus = existingRel ? existingRel.status : RelationshipStatus.Candidate

  const getName = (id: string) => {
    if (id === PLAYER_FORM_ID) return npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
    return npcProfiles.get(id)?.name || 'Unknown NPC'
  }

  const selectedOption = statusOptions.find((o) => o.status === selectedTargetStatus)
  const canAssign = selectedTargetStatus !== null && (selectedOption?.enabled ?? false)

  const handleAssign = () => {
    if (canAssign && selectedTargetStatus !== null) {
      assignRelationship(npcA, npcB, selectedTargetStatus)
      setSuccessFeedback(true)
      setTimeout(() => setSuccessFeedback(false), 2000)
    }
  }

  const handleRemove = () => {
    removeRelationship(npcA, npcB)
    setSelectedTargetStatus(null)
  }

  return (
    <TooltipProvider delayDuration={150}>
      <div className="w-[18rem] flex-shrink-0 bg-panel border border-border-subtle rounded-lg flex flex-col h-full overflow-hidden p-[0.75rem] space-y-[0.75rem]">
        {/* Target Pair Header */}
        <div className="bg-panel-raised border border-border-subtle rounded p-[0.625rem] text-center space-y-[0.25rem]">
          <div className="text-xs font-semibold text-accent-bright flex items-center justify-center gap-[0.375rem]">
            <span>{getName(npcA)}</span>
            <ArrowRightLeft className="w-[0.875em] h-[0.875em] text-accent" />
            <span>{getName(npcB)}</span>
          </div>
          <div className="text-[0.6875rem] text-foreground-muted flex items-center justify-center gap-[0.375rem]">
            <span>Current:</span>
            <StatusBadge status={currentStatus} />
            {existingRel?.isOpen && (
              <span className="text-[0.5625rem] bg-emerald-950/60 text-emerald-400 border border-emerald-800/80 px-[0.25rem] py-[0.0625rem] rounded font-semibold">
                Open
              </span>
            )}
          </div>
        </div>

        {/* Bypass Rules Toggle Card */}
        <div className="bg-panel-inset border border-border-subtle rounded p-[0.5rem] flex items-center justify-between gap-[0.5rem]">
          <div className="flex items-center gap-[0.375rem] text-xs min-w-0">
            {bypassRules ? (
              <ShieldOff className="w-[1.125em] h-[1.125em] text-status-warning flex-shrink-0" />
            ) : (
              <ShieldCheck className="w-[1.125em] h-[1.125em] text-status-success flex-shrink-0" />
            )}
            <div className="flex flex-col min-w-0">
              <span className="font-semibold text-foreground-primary text-xs truncate">
                Bypass Status Rules
              </span>
              <span className="text-[0.625rem] text-foreground-muted truncate">
                {bypassRules ? 'All statuses unlocked' : 'Enforce progression rules'}
              </span>
            </div>
          </div>
          <ToggleSwitch checked={bypassRules} onChange={setBypassRules} />
        </div>

        {/* Status Selection Buttons */}
        <ScrollArea className="flex-1 min-h-0 pr-[0.25rem]">
          <div className="space-y-[0.375rem]">
            <div className="text-xs font-semibold uppercase tracking-wider text-foreground-muted mb-[0.25rem]">
              Select New Status
            </div>

            {statusOptions.map((opt) => {
              const isSelected = selectedTargetStatus === opt.status

              const btn = (
                <button
                  key={opt.status}
                  disabled={!opt.enabled}
                  onClick={() => setSelectedTargetStatus(opt.status)}
                  className={`w-full flex items-center justify-between p-[0.5rem] rounded text-xs transition-all ${!opt.enabled
                      ? 'opacity-40 bg-panel-inset text-foreground-disabled cursor-not-allowed border border-transparent'
                      : isSelected
                        ? 'bg-accent-soft text-accent-bright border border-accent font-semibold shadow-sm'
                        : 'bg-panel-raised text-foreground-primary border border-border-subtle hover:border-border-strong hover:bg-interactive-hover cursor-pointer'
                    }`}
                >
                  <span className="flex items-center gap-[0.375rem] min-w-0">
                    <StatusIcon status={opt.status} size="1em" showTooltip={false} />
                    <span className="truncate">{opt.label}</span>
                    {opt.loverType === 'allowed' && (
                      <span className="px-[0.25rem] py-[0.0625rem] rounded text-[0.5625rem] font-bold uppercase tracking-wider bg-emerald-950/60 text-emerald-400 border border-emerald-800/80">
                        Allowed
                      </span>
                    )}
                    {opt.loverType === 'cheating' && (
                      <span className="px-[0.25rem] py-[0.0625rem] rounded text-[0.5625rem] font-bold uppercase tracking-wider bg-rose-950/60 text-rose-400 border border-rose-800/80">
                        Cheating
                      </span>
                    )}
                  </span>
                  {!opt.enabled && opt.tooltip && (
                    <span className="text-[0.625rem] text-status-warning flex items-center gap-[0.125rem] max-w-[9rem] truncate ml-auto flex-shrink-0">
                      <AlertCircle className="w-[0.875em] h-[0.875em] flex-shrink-0" />
                      <span className="truncate">{opt.tooltip}</span>
                    </span>
                  )}
                </button>
              )

              if (opt.tooltip) {
                return (
                  <Tooltip key={opt.status}>
                    <TooltipTrigger asChild>{btn}</TooltipTrigger>
                    <TooltipContent side="right" className="max-w-[18rem] z-[100] text-xs">
                      <p>{opt.tooltip}</p>
                    </TooltipContent>
                  </Tooltip>
                )
              }

              return btn
            })}
          </div>
        </ScrollArea>

        {/* Action Footer */}
        <div className="space-y-[0.375rem] border-t border-border-divider pt-[0.5rem]">
          <Button
            variant="default"
            size="sm"
            disabled={!canAssign}
            onClick={handleAssign}
            className="w-full flex items-center justify-center gap-[0.375rem]"
          >
            {successFeedback ? (
              <>
                <Check className="w-[1em] h-[1em] text-status-success" />
                <span>Status Assigned!</span>
              </>
            ) : (
              <span>Apply Status Change</span>
            )}
          </Button>

          {existingRel && (
            <Button
              variant="danger"
              size="sm"
              onClick={handleRemove}
              className="w-full flex items-center justify-center gap-[0.375rem] text-xs"
            >
              <span>Sever / Remove Relationship</span>
            </Button>
          )}
        </div>
      </div>
    </TooltipProvider>
  )
}
