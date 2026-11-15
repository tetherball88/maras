import React from 'react'
import { Home, Baby, Heart, X, Check, MapPin } from 'lucide-react'
import { PlayerHouse } from '../../types'
import { ScrollArea } from '@/components/ui/scroll-area'

export interface HouseOccupantInfo extends PlayerHouse {
  childrenCount: number
  spousesCount: number
}

interface HouseSelectionModalProps {
  isOpen: boolean
  onClose: () => void
  onSelectHouse: (house: PlayerHouse) => void
  childName: string
  spouseName?: string | null
  houses: HouseOccupantInfo[]
}

export const HouseSelectionModal: React.FC<HouseSelectionModalProps> = ({
  isOpen,
  onClose,
  onSelectHouse,
  childName,
  spouseName,
  houses,
}) => {
  if (!isOpen) return null

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 backdrop-blur-sm p-[1rem] animate-in fade-in duration-200">
      <div
        className="bg-panel border border-border-accent/40 rounded-xl shadow-2xl max-w-lg w-full max-h-[85vh] flex flex-col overflow-hidden text-foreground-primary font-ui"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-[1.25rem] border-b border-border-divider flex items-center justify-between bg-panel-raised">
          <div className="flex items-center gap-[0.75rem]">
            <div className="p-[0.5rem] rounded-full bg-accent-soft text-accent border border-border-accent">
              <Home className="w-[1.25rem] h-[1.25rem]" />
            </div>
            <div>
              <h2 className="text-base font-bold text-accent-bright">
                Select Residence for {childName}
              </h2>
              <p className="text-xs text-foreground-muted">
                {spouseName
                  ? `Assign a home for ${childName} and ${spouseName}. ${spouseName} will live here with their own schedule.`
                  : `Choose which of your properties ${childName} will reside in.`}
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-[0.375rem] text-foreground-muted hover:text-foreground-primary hover:bg-panel-inset rounded-lg transition-colors cursor-pointer"
            title="Cancel"
          >
            <X className="w-[1.25rem] h-[1.25rem]" />
          </button>
        </div>

        {/* Houses List */}
        <ScrollArea className="flex-1 p-[1.25rem] overflow-y-auto">
          <div className="space-y-[0.75rem]">
            {houses.map((house) => (
              <div
                key={house.formId}
                onClick={() => onSelectHouse(house)}
                className="group p-[1rem] rounded-lg border border-border-divider bg-panel-inset hover:bg-panel-raised hover:border-border-accent transition-all cursor-pointer flex items-center justify-between gap-[1rem] shadow-sm hover:shadow-md"
              >
                <div className="space-y-[0.375rem]">
                  <div className="flex items-center gap-[0.5rem]">
                    <span className="font-bold text-sm text-foreground-primary group-hover:text-accent-bright transition-colors">
                      {house.name}
                    </span>
                    <span className="text-[0.6875rem] font-mono text-foreground-muted">
                      ({house.formId})
                    </span>
                  </div>

                  {house.locationName && (
                    <div className="text-xs text-foreground-muted flex items-center gap-[0.25rem]">
                      <MapPin className="w-[0.875rem] h-[0.875rem] text-foreground-muted" />
                      <span>{house.locationName}</span>
                    </div>
                  )}

                  {/* Occupant Badges */}
                  <div className="flex items-center gap-[0.5rem] pt-[0.25rem] flex-wrap">
                    <span
                      className={`px-[0.5rem] py-[0.1875rem] rounded-md text-[0.6875rem] font-semibold flex items-center gap-[0.25rem] border ${house.childrenCount > 0
                        ? 'bg-accent-soft text-accent-bright border-border-accent'
                        : 'bg-panel-raised text-foreground-muted border-border-subtle'
                        }`}
                    >
                      <Baby className="w-[0.875rem] h-[0.875rem]" />
                      <span>
                        {house.childrenCount === 1
                          ? '1 Child'
                          : `${house.childrenCount} Children`}
                      </span>
                    </span>

                    <span
                      className={`px-[0.5rem] py-[0.1875rem] rounded-md text-[0.6875rem] font-semibold flex items-center gap-[0.25rem] border ${house.spousesCount > 0
                        ? 'bg-emerald-500/20 text-emerald-300 border-emerald-500/30'
                        : 'bg-panel-raised text-foreground-muted border-border-subtle'
                        }`}
                    >
                      <Heart className="w-[0.875rem] h-[0.875rem]" />
                      <span>
                        {house.spousesCount === 1
                          ? '1 Spouse'
                          : `${house.spousesCount} Spouses`}
                      </span>
                    </span>
                  </div>
                </div>

                <button
                  type="button"
                  className="px-[0.875rem] py-[0.5rem] rounded-md text-xs font-semibold bg-accent hover:bg-accent-bright text-panel transition-colors flex items-center gap-[0.375rem] flex-shrink-0 cursor-pointer shadow-sm group-hover:scale-102"
                >
                  <Check className="w-[0.875rem] h-[0.875rem]" />
                  <span>Choose House</span>
                </button>
              </div>
            ))}

            {houses.length === 0 && (
              <div className="p-[2rem] text-center text-xs text-foreground-muted bg-panel-inset border border-border-subtle rounded-lg">
                No player houses registered.
              </div>
            )}
          </div>
        </ScrollArea>

        {/* Footer */}
        <div className="p-[0.875rem] px-[1.25rem] border-t border-border-divider bg-panel-raised flex justify-end gap-[0.5rem]">
          <button
            onClick={onClose}
            className="px-[0.875rem] py-[0.375rem] text-xs font-semibold text-foreground-secondary hover:text-foreground-primary bg-panel-inset hover:bg-panel-inset/80 border border-border-divider rounded-md transition-colors cursor-pointer"
          >
            Cancel
          </button>
        </div>
      </div>
    </div>
  )
}
