import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SelectInput } from '../../shared/SelectInput'
import { ToggleSwitch } from '../../shared/ToggleSwitch'
import { Home } from 'lucide-react'
import { PLAYER_FORM_ID } from '../../../data/mockData'
import { areSameFormId } from '../../../utils/statusAvailability'

interface SpouseHousingSectionProps {
  npcId: string
  className?: string
}

export const SpouseHousingSection: React.FC<SpouseHousingSectionProps> = ({
  npcId,
  className = '',
}) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const playerHomes = useMarasStore((s) => s.playerHomes)
  const children = useMarasStore((s) => s.children)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)

  const npc = npcProfiles.get(npcId)

  const hasChildrenWithPlayer = Array.from(children.values()).some(
    (c) =>
      (areSameFormId(c.guardianAId, PLAYER_FORM_ID) && areSameFormId(c.guardianBId, npcId)) ||
      (areSameFormId(c.guardianBId, PLAYER_FORM_ID) && areSameFormId(c.guardianAId, npcId)) ||
      (areSameFormId(c.bioMotherId, PLAYER_FORM_ID) && areSameFormId(c.bioFatherId, npcId)) ||
      (areSameFormId(c.bioFatherId, PLAYER_FORM_ID) && areSameFormId(c.bioMotherId, npcId)),
  )

  const availableHomes = [
    ...(hasChildrenWithPlayer ? [] : [{ value: 'Unset', label: 'Unset (No assigned house)' }]),
    ...playerHomes.map((h) => ({
      value: h.name,
      label: h.locationName ? `${h.name} (${h.locationName})` : h.name,
    })),
  ]

  const hasAssignedHome = Boolean(npc?.currentHome && npc.currentHome !== 'Unset')
  const hasOriginalHome = Boolean(npc?.originalHome)

  return (
    <SectionCard title="Assigned Residence & Housing" icon={Home} className={className}>
      <div className="pt-[0.25rem] space-y-[0.5rem] text-xs">
        <div className="flex flex-col sm:flex-row items-stretch gap-[0.75rem]">
          {/* 1. Assigned Player House Dropdown */}
          <div className="flex-1 min-w-0 space-y-[0.25rem]">
            <label className="text-foreground-muted block text-xs font-medium">
              Assigned Player Home
            </label>
            <SelectInput
              value={
                npc?.currentHome || 'Unset'
              }
              options={availableHomes}
              onChange={(val) =>
                updateNPCProfile(npcId, {
                  currentHome: val === 'Unset' ? undefined : val,
                })
              }
              className="w-full text-xs h-8"
            />
          </div>

          {/* 2. Stay at Assigned Home (present when home is assigned) */}
          {hasAssignedHome && (
            <div className="flex-1 min-w-0 space-y-[0.25rem]">
              <div className="flex items-center justify-between">
                <label
                  htmlFor="stay-at-home-toggle"
                  className="text-foreground-muted block text-xs font-medium cursor-pointer"
                >
                  Stay at Assigned Home
                </label>
                <span className="text-[0.6875rem] text-foreground-muted hidden lg:inline truncate">
                  {npc?.stayAtHome !== false ? 'Sandboxes at home' : 'Original routine'}
                </span>
              </div>
              <div className="flex h-8 w-full items-center justify-between rounded-md border border-border-subtle bg-panel-inset px-3 py-1 text-xs">
                <span className="text-[0.6875rem] text-foreground-secondary truncate mr-2">
                  {npc?.stayAtHome !== false ? 'Stay at home' : 'Normal routine'}
                </span>
                <ToggleSwitch
                  checked={npc?.stayAtHome !== false}
                  onChange={(val) =>
                    updateNPCProfile(npcId, { stayAtHome: val })
                  }
                />
              </div>
            </div>
          )}

          {/* 3. Original Home (present when original home exists) */}
          {hasOriginalHome && (
            <div className="flex-1 min-w-0 space-y-[0.25rem]">
              <label className="text-foreground-muted block text-xs font-medium">
                Original Home
              </label>
              <div className="flex h-8 w-full items-center justify-between rounded-md border border-border-subtle bg-panel-inset px-3 py-1 text-xs">
                <span
                  className="text-xs text-foreground-secondary font-medium truncate"
                  title={npc?.originalHome}
                >
                  {npc?.originalHome}
                </span>
                {hasAssignedHome && (
                  <button
                    disabled={hasChildrenWithPlayer}
                    onClick={() =>
                      !hasChildrenWithPlayer &&
                      updateNPCProfile(npcId, { currentHome: undefined })
                    }
                    className={`text-[0.625rem] font-semibold px-[0.375rem] py-[0.125rem] rounded border transition-colors flex-shrink-0 ml-2 ${hasChildrenWithPlayer
                      ? 'bg-muted/10 text-foreground-muted border-border-divider cursor-not-allowed opacity-60'
                      : 'bg-destructive/10 hover:bg-destructive/20 text-destructive border-destructive/20 cursor-pointer'
                      }`}
                    title={
                      hasChildrenWithPlayer
                        ? 'Cannot send spouse away while you have children together'
                        : "Free from player's house (Return to original home)"
                    }
                  >
                    {hasChildrenWithPlayer ? 'Locked' : 'Return'}
                  </button>
                )}
              </div>
            </div>
          )}
        </div>

        {/* Warning note if spouse has children and cannot be returned */}
        {hasChildrenWithPlayer && (hasAssignedHome ? (
          <p className="text-[0.6875rem] text-amber-500/90 italic pt-[0.125rem]">
            * Cannot return spouse to original home while you have children together. Move them to a different player home instead.
          </p>
        ) : (<p className="text-[0.6875rem] text-amber-500/90 italic pt-[0.125rem]">
          * When child will grow and be spawned in game world you will have to select house for them, and their mother, your spouse, will move there.
        </p>))}
      </div>
    </SectionCard>
  )
}
