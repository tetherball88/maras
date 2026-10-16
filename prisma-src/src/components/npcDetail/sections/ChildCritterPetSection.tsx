import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { ToggleSwitch } from '../../shared/ToggleSwitch'
import { Shield, AlertTriangle } from 'lucide-react'
import { areSameFormId } from '../../../utils/statusAvailability'
import { ChildStage } from '../../../types'

interface ChildCritterPetSectionProps {
  npcId: string
  className?: string
}

export const ChildCritterPetSection: React.FC<ChildCritterPetSectionProps> = ({
  npcId,
  className = '',
}) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const children = useMarasStore((s) => s.children)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const settings = useMarasStore((s) => s.settings)
  const dismissChildCritter = useMarasStore((s) => s.dismissChildCritter)
  const dismissChildPet = useMarasStore((s) => s.dismissChildPet)

  const npc = npcProfiles.get(npcId)
  const childRecord = children.get(npcId)

  const stage: ChildStage = childRecord?.stage || npc?.stage || 'child'
  const hasRefID = childRecord?.hasRefID ?? npc?.hasRefID ?? !npcId.startsWith('FF7')

  if (!hasRefID || stage !== 'child') return null

  return (
    <SectionCard title="Critter & Pet Management" icon={Shield} className={className}>
      <div className="pt-[0.25rem] space-y-[0.75rem] text-xs">
        {/* Global Ban Alert Banner */}
        {(settings.globalBanCritter || settings.globalBanPet) && (
          <div className="p-[0.5rem] bg-amber-500/10 border border-amber-500/30 rounded text-amber-300 flex items-start gap-[0.5rem]">
            <AlertTriangle className="w-[1.125em] h-[1.125em] flex-shrink-0 mt-[0.125rem]" />
            <div>
              <span className="font-semibold block">Global Ban Override Active</span>
              <span className="text-[0.6875rem] text-amber-300/80">
                {settings.globalBanCritter && settings.globalBanPet
                  ? 'Critters & Pets are globally banned for all children.'
                  : settings.globalBanCritter
                    ? 'Critters are globally banned for all children.'
                    : 'Pets are globally banned for all children.'}
              </span>
            </div>
          </div>
        )}

        {/* Wild Critter Management */}
        {npc?.currentCritter ? (
          <div className="p-[0.625rem] bg-panel-inset border border-border-subtle rounded flex items-center justify-between gap-[0.5rem]">
            <div>
              <div className="text-[0.6875rem] text-foreground-muted font-medium">Adopted Critter</div>
              <div className="font-bold text-accent-bright text-sm mt-[0.125rem] flex items-center gap-[0.375rem]">
                <span>{npc.currentCritter.name}</span>
                <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-raised px-[0.25rem] py-[0.125rem] rounded border border-border-subtle">
                  {npc.currentCritter.formId}
                </span>
              </div>
            </div>
            <button
              onClick={() => dismissChildCritter(npcId)}
              className="text-xs font-semibold text-red-300 bg-red-500/20 hover:bg-red-500/30 px-[0.625rem] py-[0.375rem] rounded border border-red-500/40 transition-colors flex items-center gap-[0.25rem] cursor-pointer"
              title="Disables and removes critter actor from the game"
            >
              <span>Dismiss & Remove Critter</span>
            </button>
          </div>
        ) : (
          <div className="flex justify-between items-center bg-panel-inset p-[0.625rem] rounded border border-border-subtle">
            <div>
              <div className="font-semibold text-foreground-primary">Ban Critter Adoption</div>
              <div className="text-[0.6875rem] text-foreground-muted">
                No active critter. Prevent wild critter adoption dialogue events (foxes, hares, mudcrabs) for this child.
              </div>
            </div>
            <ToggleSwitch
              checked={!!npc?.critterBanned}
              onChange={(val) => updateNPCProfile(npcId, { critterBanned: val })}
            />
          </div>
        )}

        {/* Household Pet Management */}
        {npc?.currentPet ? (
          <div className="p-[0.625rem] bg-panel-inset border border-border-subtle rounded flex items-center justify-between gap-[0.5rem]">
            <div>
              <div className="text-[0.6875rem] text-foreground-muted font-medium">Adopted Household Pet</div>
              <div className="font-bold text-accent-bright text-sm mt-[0.125rem] flex items-center gap-[0.375rem]">
                <span>{npc.currentPet.name}</span>
                <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-raised px-[0.25rem] py-[0.125rem] rounded border border-border-subtle">
                  {npc.currentPet.formId}
                </span>
              </div>
            </div>
            <button
              onClick={() => dismissChildPet(npcId)}
              className="text-xs font-semibold text-amber-300 bg-amber-500/20 hover:bg-amber-500/30 px-[0.625rem] py-[0.375rem] rounded border border-amber-500/40 transition-colors flex items-center gap-[0.25rem] cursor-pointer"
              title="Unbinds pet from child while keeping pet alive in world"
            >
              <span>Dismiss Pet</span>
            </button>
          </div>
        ) : (
          <div className="flex justify-between items-center bg-panel-inset p-[0.625rem] rounded border border-border-subtle">
            <div>
              <div className="font-semibold text-foreground-primary">Ban Pet Adoption</div>
              <div className="text-[0.6875rem] text-foreground-muted">
                No active pet. Prevent household pet adoption dialogue events (dogs) for this child.
              </div>
            </div>
            <ToggleSwitch
              checked={!!npc?.petBanned}
              onChange={(val) => updateNPCProfile(npcId, { petBanned: val })}
            />
          </div>
        )}
      </div>
    </SectionCard>
  )
}
