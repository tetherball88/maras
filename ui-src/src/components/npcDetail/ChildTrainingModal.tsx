import React, { useState, useMemo } from 'react'
import {
  GraduationCap,
  X,
  Coins,
  Lock,
  CheckCircle2,
  MapPin,
  Search,
  Sparkles,
  Swords,
} from 'lucide-react'
import { TrainingCatalogItem, TrainingDiscountEntry } from '../../types'

interface ChildTrainingModalProps {
  isOpen: boolean
  onClose: () => void
  onSelectTraining: (classIndex: number) => void
  childName: string
  hasRefID: boolean
  hasBeastblood?: boolean
  trainingDiscounts?: Record<string, TrainingDiscountEntry>
  isPlayerChild: boolean
  durationDays: number
  baseCost: number
  playerGold: number
  requireFaction: boolean
  trainingCatalog: TrainingCatalogItem[]
  isEntryUnlocked: (item: TrainingCatalogItem) => boolean
}

export const ChildTrainingModal: React.FC<ChildTrainingModalProps> = ({
  isOpen,
  onClose,
  onSelectTraining,
  childName,
  hasRefID,
  hasBeastblood = false,
  trainingDiscounts,
  isPlayerChild,
  durationDays,
  baseCost,
  playerGold,
  requireFaction,
  trainingCatalog,
  isEntryUnlocked,
}) => {
  const [searchQuery, setSearchQuery] = useState('')
  const [failedImages, setFailedImages] = useState<Record<number, boolean>>({})

  const filteredCatalog = useMemo(() => {
    const q = searchQuery.trim().toLowerCase()
    const matching = !q
      ? trainingCatalog
      : trainingCatalog.filter(
          (item) =>
            item.className.toLowerCase().includes(q) ||
            item.location.toLowerCase().includes(q) ||
            (item.description && item.description.toLowerCase().includes(q)) ||
            (item.factionLabel && item.factionLabel.toLowerCase().includes(q)),
        )

    const decorated = matching.map((item) => {
      const baseTuition = isPlayerChild
        ? item.effectiveCost !== undefined
          ? item.effectiveCost
          : Math.max(0, Math.round(baseCost * item.costMultiplier))
        : 0
      const discountInfo = trainingDiscounts?.[String(item.index)]
      const discountPercent =
        discountInfo && discountInfo.percent > 0 ? Math.min(100, discountInfo.percent) : 0
      const cost =
        isPlayerChild && discountPercent > 0
          ? Math.max(0, Math.round(baseTuition * (1 - discountPercent / 100)))
          : baseTuition
      const canAfford = !isPlayerChild || cost <= 0 || playerGold >= cost
      const unlocked = isEntryUnlocked(item)
      const isBeastbloodBlocked = Boolean(item.requiresBeastblood && !hasBeastblood)
      const isFactionBlocked = Boolean(
        requireFaction &&
          (item.factionLabel || item.factionKey || item.requirementMet === false) &&
          !unlocked,
      )
      const isRequirementBlocked = isBeastbloodBlocked || isFactionBlocked
      const canSend = canAfford && !isRequirementBlocked
      return {
        item,
        baseTuition,
        cost,
        discountPercent,
        discountReason: discountInfo?.reason,
        canAfford,
        unlocked,
        isBeastbloodBlocked,
        isFactionBlocked,
        isRequirementBlocked,
        canSend,
      }
    })

    decorated.sort((a, b) => {
      if (a.canSend !== b.canSend) {
        return a.canSend ? -1 : 1
      }
      return a.item.index - b.item.index
    })

    return decorated
  }, [
    trainingCatalog,
    searchQuery,
    isPlayerChild,
    baseCost,
    playerGold,
    isEntryUnlocked,
    hasBeastblood,
    trainingDiscounts,
    requireFaction,
  ])

  if (!isOpen) return null

  return (
    <div
      className="fixed inset-0 z-modal flex items-center justify-center bg-black/80 p-[1.25rem]"
      onClick={onClose}
    >
      <div
        className="bg-panel border border-border-accent/50 rounded-xl shadow-2xl max-w-[54rem] w-full max-h-[85vh] flex flex-col overflow-hidden text-foreground-primary font-ui"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-[1rem] border-b border-border-divider flex items-center justify-between gap-[1rem] bg-panel-raised">
          <div className="flex items-center gap-[0.75rem]">
            <div className="p-[0.5rem] rounded-lg bg-purple-500/15 text-purple-300 border border-purple-500/35">
              <GraduationCap className="w-[1.375rem] h-[1.375rem]" />
            </div>
            <div>
              <h2 className="text-sm font-bold text-accent-bright">
                Send {childName} to Adolescent Training
              </h2>
              <p className="text-[0.6875rem] text-foreground-muted">
                Choose an organization, academy, or Daedric covenant ({durationDays} day training curriculum)
              </p>
            </div>
          </div>

          <div className="flex items-center gap-[0.75rem]">
            {isPlayerChild && baseCost > 0 && (
              <div className="flex items-center gap-[0.375rem] px-[0.625rem] py-[0.3125rem] rounded-lg bg-amber-500/15 border border-amber-500/30 text-amber-300 text-xs font-semibold">
                <Coins className="w-[1em] h-[1em] text-amber-400" />
                <span>Your Gold: {playerGold.toLocaleString()}</span>
              </div>
            )}
            <button
              onClick={onClose}
              className="p-[0.375rem] text-foreground-muted hover:text-foreground-primary hover:bg-panel-inset rounded-lg transition-colors cursor-pointer"
              title="Close"
            >
              <X className="w-[1.25rem] h-[1.25rem]" />
            </button>
          </div>
        </div>

        {/* Search & Info Subbar */}
        <div className="px-[1rem] py-[0.625rem] bg-panel-inset/60 border-b border-border-divider flex items-center justify-between gap-[0.75rem]">
          <div className="relative flex-1 max-w-[22rem]">
            <Search className="w-[0.875rem] h-[0.875rem] text-foreground-muted absolute left-[0.625rem] top-1/2 -translate-y-1/2 pointer-events-none" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="Search training location, class, or patron..."
              className="w-full pl-[2rem] pr-[0.75rem] py-[0.3125rem] bg-panel border border-border-subtle rounded text-xs text-foreground-primary placeholder:text-foreground-muted focus:outline-none focus:border-accent"
            />
          </div>

          {hasRefID && (
            <span className="text-[0.6875rem] text-foreground-muted">
              Despawns young child actor &amp; unlocks adult adventurer after {durationDays} day(s)
            </span>
          )}
        </div>

        {/* Location List */}
        <div className="p-[1rem] overflow-y-auto space-y-[0.625rem] flex-1">
          {filteredCatalog.length === 0 ? (
            <div className="p-[2rem] text-center text-xs text-foreground-muted">
              No training locations match &ldquo;{searchQuery}&rdquo;.
            </div>
          ) : (
            filteredCatalog.map(
              ({
                item,
                baseTuition,
                cost,
                discountPercent,
                discountReason,
                canAfford,
                unlocked,
                isBeastbloodBlocked,
                isFactionBlocked,
                isRequirementBlocked,
                canSend,
              }) => {
                const showImage = Boolean(item.image && !failedImages[item.index])
                const isDaedric = Boolean(item.factionKey && item.factionKey.startsWith('champion'))

                return (
                  <div
                    key={item.index}
                    className={`flex items-stretch gap-[0.875rem] p-[0.75rem] rounded-lg border transition-opacity ${
                      canSend
                        ? 'bg-panel-raised/90 border-border-subtle hover:border-purple-500/45'
                        : 'bg-panel-inset/30 border-border-subtle/35 opacity-45'
                    }`}
                  >
                    {/* Location Image / Fallback */}
                    <div
                      className={`w-[9rem] h-[5rem] rounded-md overflow-hidden border border-border-subtle bg-panel-inset flex-shrink-0 relative flex items-center justify-center ${
                        canSend ? '' : 'opacity-60'
                      }`}
                    >
                      {showImage ? (
                        <img
                          src={item.image}
                          alt={`${item.className} — ${item.location}`}
                          className="w-full h-full object-cover"
                          onError={() =>
                            setFailedImages((prev) => ({ ...prev, [item.index]: true }))
                          }
                        />
                      ) : (
                        <div className="w-full h-full flex flex-col items-center justify-center p-[0.375rem] text-center bg-gradient-to-br from-purple-950/40 via-panel-inset to-amber-950/30">
                          {isDaedric ? (
                            <Sparkles className="w-[1.25rem] h-[1.25rem] text-amber-400/70 mb-[0.25rem]" />
                          ) : (
                            <Swords className="w-[1.25rem] h-[1.25rem] text-purple-400/70 mb-[0.25rem]" />
                          )}
                          <span className="text-[0.625rem] font-semibold text-foreground-secondary leading-tight line-clamp-2">
                            {item.location}
                          </span>
                        </div>
                      )}
                    </div>

                    {/* Location Details: Class Title, Location Badge, Description */}
                    <div className="flex-1 min-w-0 flex flex-col justify-between">
                      <div>
                        <div className="flex items-center flex-wrap gap-[0.5rem]">
                          <span
                            className={`text-xs font-bold ${
                              canSend ? 'text-accent-bright' : 'text-foreground-muted'
                            }`}
                          >
                            {item.className}
                          </span>
                        <span className="inline-flex items-center gap-[0.25rem] px-[0.4375rem] py-[0.0625rem] rounded text-[0.6875rem] font-semibold bg-purple-500/20 text-purple-200 border border-purple-500/35">
                          <MapPin className="w-[0.85em] h-[0.85em]" />
                          <span>{item.location}</span>
                        </span>

                        {item.requiresBeastblood && (
                          <span
                            className={`inline-flex items-center gap-[0.25rem] px-[0.4375rem] py-[0.0625rem] rounded text-[0.625rem] font-semibold border ${
                              hasBeastblood
                                ? 'bg-emerald-500/15 text-emerald-300 border-emerald-500/30'
                                : 'bg-rose-500/20 text-rose-300 border-rose-500/40'
                            }`}
                          >
                            {hasBeastblood ? (
                              <CheckCircle2 className="w-[0.85em] h-[0.85em]" />
                            ) : (
                              <Lock className="w-[0.85em] h-[0.85em]" />
                            )}
                            <span>🐺 Requires Beastblood</span>
                          </span>
                        )}

                        {item.factionLabel && (
                          <span
                            className={`inline-flex items-center gap-[0.25rem] px-[0.4375rem] py-[0.0625rem] rounded text-[0.625rem] font-semibold border ${
                              unlocked
                                ? 'bg-emerald-500/15 text-emerald-300 border-emerald-500/30'
                                : isFactionBlocked
                                  ? 'bg-rose-500/20 text-rose-300 border-rose-500/40'
                                  : 'bg-amber-500/15 text-amber-300 border-amber-500/30'
                            }`}
                          >
                            {unlocked ? (
                              <CheckCircle2 className="w-[0.85em] h-[0.85em]" />
                            ) : isFactionBlocked ? (
                              <Lock className="w-[0.85em] h-[0.85em]" />
                            ) : null}
                            <span>{item.factionLabel}</span>
                          </span>
                        )}

                        {discountPercent > 0 && (
                          <span className="inline-flex items-center gap-[0.25rem] px-[0.4375rem] py-[0.0625rem] rounded text-[0.625rem] font-semibold bg-emerald-500/20 text-emerald-200 border border-emerald-500/40">
                            <span>
                              -{discountPercent}% {discountReason || 'Parent Affiliation'}
                            </span>
                          </span>
                        )}
                      </div>

                      {item.description && (
                        <p className="text-[0.6875rem] text-foreground-secondary mt-[0.25rem] leading-relaxed">
                          {item.description}
                        </p>
                      )}
                    </div>

                    <div className="flex items-center gap-[0.75rem] mt-[0.375rem] text-[0.625rem] text-foreground-muted">
                      <span className="inline-flex items-center gap-[0.25rem]">
                        <MapPin className="w-[0.875em] h-[0.875em] text-purple-400" />
                        <span>{item.location}</span>
                      </span>
                      {isBeastbloodBlocked && (
                        <span className="text-rose-300 font-medium">
                          Requires {childName} to carry Hircine&apos;s Beastblood (inherited from a werewolf parent)
                        </span>
                      )}
                      {!isBeastbloodBlocked && isFactionBlocked && item.factionLabel && (
                        <span className="text-rose-300 font-medium">
                          Requires Player membership/patronage: {item.factionLabel}
                        </span>
                      )}
                      {!canAfford && (
                        <span className="text-rose-300 font-medium">
                          Insufficient gold ({playerGold} / {cost}g)
                        </span>
                      )}
                    </div>
                  </div>

                  {/* Cost & Send to Training CTA */}
                  <div className="flex flex-col items-end justify-between flex-shrink-0 pl-[0.5rem] border-l border-border-divider/60 min-w-[9.5rem]">
                    <div className="text-right">
                      {isPlayerChild && baseCost > 0 ? (
                        <>
                          <div
                            className={`inline-flex items-center gap-[0.25rem] text-xs font-bold ${
                              canAfford ? 'text-amber-300' : 'text-rose-300'
                            }`}
                          >
                            <Coins className="w-[0.95em] h-[0.95em]" />
                            {discountPercent > 0 && baseTuition !== cost && (
                              <span className="line-through text-foreground-muted font-normal text-[0.6875rem] mr-[0.125rem]">
                                {baseTuition.toLocaleString()}
                              </span>
                            )}
                            <span>{cost.toLocaleString()} Gold</span>
                          </div>
                          <div className="text-[0.625rem] text-foreground-muted">
                            Tuition ({Number(item.costMultiplier.toFixed(2))}x rate
                            {discountPercent > 0 ? ` · -${discountPercent}%` : ''})
                          </div>
                        </>
                      ) : (
                        <div className="text-[0.6875rem] text-emerald-300 font-semibold">
                          No Tuition Cost
                        </div>
                      )}
                    </div>

                    <button
                      onClick={() => {
                        if (!canSend) return
                        onSelectTraining(item.index)
                        onClose()
                      }}
                      disabled={!canSend}
                      className={`w-full justify-center text-xs font-semibold px-[0.75rem] py-[0.4375rem] rounded border transition-colors flex items-center gap-[0.375rem] ${
                        canSend
                          ? 'text-purple-100 bg-purple-600/30 hover:bg-purple-600/50 border-purple-400/50 cursor-pointer'
                          : 'opacity-50 cursor-not-allowed bg-panel-inset text-foreground-muted border-border-subtle'
                      }`}
                    >
                      {isRequirementBlocked ? (
                        <>
                          <Lock className="w-[0.95em] h-[0.95em]" />
                          <span>Locked</span>
                        </>
                      ) : (
                        <>
                          <GraduationCap className="w-[0.95em] h-[0.95em] text-purple-300" />
                          <span>Send to Training</span>
                        </>
                      )}
                    </button>
                  </div>
                </div>
              )
            })
          )}
        </div>

        {/* Footer */}
        <div className="px-[1rem] py-[0.75rem] bg-panel-raised border-t border-border-divider flex items-center justify-between text-xs">
          <span className="text-[0.6875rem] text-foreground-muted">
            Showing {filteredCatalog.length} of {trainingCatalog.length} training locations
          </span>
          <button
            onClick={onClose}
            className="px-[0.875rem] py-[0.375rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded text-foreground-secondary text-xs font-semibold transition-colors cursor-pointer"
          >
            Cancel
          </button>
        </div>
      </div>
    </div>
  )
}
