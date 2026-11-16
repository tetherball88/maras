import React, { useState } from 'react'
import { BuffItem } from '../../types'
import { Badge } from '@/components/ui/badge'
import { ChevronDown, ChevronRight, User, ShieldAlert, Sparkles, UserPlus } from 'lucide-react'
import { t as tImperative, useTranslation } from '../../i18n'

export function getBuffCategoryLabel(category: string): string {
  const lower = (category || '').trim().toLowerCase()
  switch (lower) {
    case 'outcast':
      return tImperative('enums.socialClass.outcast')
    case 'poverty':
      return tImperative('enums.socialClass.poverty')
    case 'working':
      return tImperative('enums.socialClass.working')
    case 'middle':
      return tImperative('enums.socialClass.middle')
    case 'wealthy':
      return tImperative('enums.socialClass.wealthy')
    case 'religious':
      return tImperative('enums.socialClass.religious')
    case 'nobles':
    case 'noble':
      return tImperative('enums.socialClass.nobles')
    case 'rulers':
    case 'ruler':
      return tImperative('enums.socialClass.rulers')
    case 'warrior':
      return tImperative('enums.skillType.warrior')
    case 'mage':
      return tImperative('enums.skillType.mage')
    case 'rogue':
      return tImperative('enums.skillType.rogue')
    case 'craftsman':
      return tImperative('enums.skillType.craftsman')
    case 'ranger':
      return tImperative('enums.skillType.ranger')
    case 'orator':
      return tImperative('enums.skillType.orator')
    case 'spouses':
      return tImperative('household.buffs.categories.spouses')
    case 'children':
      return tImperative('household.buffs.categories.children')
    case 'followers':
      return tImperative('household.buffs.categories.followers')
    case 'houses':
      return tImperative('household.buffs.categories.houses')
    case 'synergy':
      return tImperative('household.buffs.categories.synergy')
    case 'permanent':
      return tImperative('household.buffs.categories.permanent')
    case 'follower':
      return tImperative('household.buffs.categories.follower')
    default:
      return category
  }
}

export const BUFF_ICON_MAP: Record<string, string> = {
  // Skill Types (Follower Buffs)
  craftsman: './assets/icons/buffs/tailored_defence.png',
  mage: './assets/icons/buffs/arcane_union.png',
  orator: './assets/icons/buffs/echoed_voice.png',
  ranger: './assets/icons/buffs/wildsightbond.png',
  rogue: './assets/icons/buffs/shared_shadows.png',
  warrior: './assets/icons/buffs/bond_of_steel.png',

  // Social Classes (Permanent Buffs)
  middle: './assets/icons/buffs/steady_kinship.png',
  nobles: './assets/icons/buffs/aristocratic_favor.png',
  outcast: './assets/icons/buffs/kiss_of_the_knife.png',
  poverty: './assets/icons/buffs/enduring_hearts.png',
  religious: './assets/icons/buffs/blessing_of_the_union.png',
  rulers: './assets/icons/buffs/speech_of_sovereigns.png',
  wealthy: './assets/icons/buffs/merchants_dowry.png',
  working: './assets/icons/buffs/labors_legacy.png',
}

interface BuffCardProps {
  buff: BuffItem
  onNpcClick?: (formId: string) => void
}

export const BuffCard: React.FC<BuffCardProps> = ({ buff, onNpcClick }) => {
  const { t } = useTranslation()
  const [isExpanded, setIsExpanded] = useState<boolean>(false)

  const iconSrc = BUFF_ICON_MAP[buff.id.toLowerCase()] || './assets/icons/buffs/bond_of_steel.png'

  // Format magnitude cleanly as positive number matching descriptive text
  const absEffectiveValue = Math.abs(buff.effectiveValue)
  const formattedEffectiveNum = Number.isInteger(absEffectiveValue)
    ? absEffectiveValue.toString()
    : absEffectiveValue.toFixed(1)

  const absBaseValue = Math.abs(buff.baseValue)
  const formattedBaseNum = Number.isInteger(absBaseValue)
    ? absBaseValue.toString()
    : absBaseValue.toFixed(1)

  const formatBuffValue = (formattedNum: string) => {
    if (buff.unit === '%') return `${formattedNum}%`
    if (buff.unit === 'points') return t('household.buffs.unitPoints', { value: formattedNum })
    return `${formattedNum} ${buff.unit}`
  }
  const valueDisplay = formatBuffValue(formattedEffectiveNum)
  const baseValueDisplay = formatBuffValue(formattedBaseNum)
  const categoryLabel = getBuffCategoryLabel(buff.category)

  const hasContributors = buff.contributors && buff.contributors.length > 0
  const hasPotentialSpouses = buff.potentialSpouses && buff.potentialSpouses.length > 0
  const canExpand = hasContributors || hasPotentialSpouses

  const getRankName = (rank?: number) => {
    if (rank === undefined || rank === null) return ''
    if (rank === 0) return t('household.buffs.rankLead')
    if (rank === 1) return t('household.buffs.rank2nd')
    if (rank === 2) return t('household.buffs.rank3rd')
    return t('household.buffs.rankN', { rank: rank + 1 })
  }

  return (
    <div
      className={`rounded-lg border transition-all duration-200 overflow-hidden flex flex-col justify-between ${buff.isActive
          ? 'bg-panel-raised border-border-accent/40 shadow-sm hover:border-accent/60'
          : 'bg-panel-inset/50 border-border-divider opacity-75 hover:opacity-95'
        }`}
    >
      {/* Main Card Header / Content */}
      <div className="p-[0.875rem] flex flex-col flex-1 justify-between gap-[0.625rem]">
        {/* Top Header: Icon + Info + Value */}
        <div className="flex items-center justify-between gap-[0.75rem] min-h-[3.25rem]">
          {/* Left: Icon & Info */}
          <div className="flex items-center gap-[0.75rem] min-w-0 flex-1">
            <div
              className={`relative w-[3rem] h-[3rem] rounded-md flex-shrink-0 flex items-center justify-center p-[0.25rem] border ${buff.isActive
                  ? 'bg-accent-soft/30 border-accent/60 shadow-[0_0_0.75rem_rgba(217,119,6,0.2)]'
                  : 'bg-panel-inset border-border-divider opacity-60 grayscale'
                }`}
            >
              <img
                src={iconSrc}
                alt={buff.name}
                className="w-full h-full object-contain filter drop-shadow"
                onError={(e) => {
                  ; (e.target as HTMLElement).style.display = 'none'
                }}
              />
            </div>

            <div className="min-w-0 flex flex-col justify-center">
              <h3 className="text-sm md:text-base font-semibold text-foreground-primary tracking-wide truncate">
                {buff.name}
              </h3>
              <div className="flex items-center gap-[0.375rem] mt-[0.25rem] flex-wrap">
                <Badge
                  variant={buff.categoryType === 'permanent' ? 'default' : 'secondary'}
                  className="text-[0.6875rem] py-0 px-[0.375rem] leading-tight"
                >
                  {categoryLabel}
                </Badge>
                {buff.isActive ? (
                  <Badge variant="success" className="text-[0.6875rem] py-0 px-[0.375rem] leading-tight">
                    {t('household.buffs.activeBadge')}
                  </Badge>
                ) : (
                  <Badge variant="secondary" className="text-[0.6875rem] py-0 px-[0.375rem] text-foreground-muted leading-tight">
                    {t('household.buffs.inactiveBadge')}
                  </Badge>
                )}
              </div>
            </div>
          </div>

          {/* Right: Value & Multiplier */}
          <div className="flex items-center gap-[0.5rem] flex-shrink-0">
            {buff.isActive ? (
              <>
                <div className="text-right flex flex-col justify-center">
                  <span className="text-base md:text-lg font-bold text-accent-bright leading-tight">
                    {valueDisplay}
                  </span>
                  <span className="text-[0.6875rem] text-foreground-muted font-mono leading-tight mt-[0.0625rem]">
                    {t('household.buffs.baseValue', { value: baseValueDisplay })}
                  </span>
                </div>
                <span className="px-[0.5rem] py-[0.1875rem] rounded bg-accent-soft text-accent-bright font-mono text-xs font-semibold border border-border-accent flex-shrink-0">
                  x{buff.multiplier.toFixed(2)}
                </span>
              </>
            ) : (
              <div className="text-right flex flex-col items-end justify-center">
                <span className="text-xs font-mono text-foreground-muted bg-panel-inset px-[0.375rem] py-[0.125rem] rounded border border-border-divider">
                  0.00x
                </span>
                <span className="text-[0.6875rem] text-foreground-muted mt-[0.125rem]">
                  {t('household.buffs.baseValue', { value: baseValueDisplay })}
                </span>
              </div>
            )}
          </div>
        </div>

        {/* Description Text with flexible height for multi-line translations */}
        <div className="text-xs text-foreground-secondary leading-relaxed bg-panel-inset/60 p-[0.5rem] rounded border border-border-divider/70 min-h-[3.75rem] flex items-center">
          <p>
            {buff.description}{' '}
            <strong className={buff.isActive ? 'text-accent-bright font-semibold' : 'text-foreground-primary'}>
              {buff.isActive ? valueDisplay : baseValueDisplay}
            </strong>
            {!buff.isActive && (
              <span className="text-foreground-muted italic ml-[0.25rem]">
                {t('household.buffs.inactiveInline')}
              </span>
            )}
          </p>
        </div>

        {/* Expandable Footer / Contributing Spouses Trigger */}
        {canExpand && (
          <div className="pt-[0.25rem] border-t border-border-divider/50 flex items-center justify-between min-h-[2rem]">
            <button
              onClick={() => setIsExpanded(!isExpanded)}
              className="flex items-center gap-[0.375rem] text-xs font-medium text-accent hover:text-accent-bright transition-colors cursor-pointer py-[0.25rem]"
            >
              {isExpanded ? (
                <ChevronDown className="w-[1em] h-[1em]" />
              ) : (
                <ChevronRight className="w-[1em] h-[1em]" />
              )}
              <span>
                {hasContributors
                  ? t('household.buffs.contributingSpouses', { count: buff.contributors.length })
                  : t('household.buffs.spousesOverview')}
                {hasPotentialSpouses && !hasContributors &&
                  t('household.buffs.availableAtHomeSuffix', { count: buff.potentialSpouses?.length ?? 0 })}
              </span>
            </button>

            <span className="text-[0.6875rem] text-foreground-muted">
              {buff.categoryType === 'permanent'
                ? t('household.buffs.scaleRankAffection')
                : t('household.buffs.scaleAffection')}
            </span>
          </div>
        )}

        {!canExpand && (
          <div className="pt-[0.25rem] border-t border-border-divider/50 text-[0.6875rem] text-foreground-muted flex items-center gap-[0.375rem] min-h-[2rem]">
            <ShieldAlert className="w-[0.9em] h-[0.9em]" />
            <span>{t('household.buffs.noMarriedSpouses')}</span>
          </div>
        )}
      </div>

      {/* Expanded Details: Contributor Spouses List */}
      {isExpanded && canExpand && (
        <div className="bg-panel-inset/80 border-t border-border-divider p-[0.75rem] space-y-[0.5rem]">
          {/* Active Contributors */}
          {hasContributors && (
            <div className="space-y-[0.375rem]">
              <div className="text-[0.6875rem] uppercase font-semibold text-foreground-muted tracking-wider flex items-center gap-[0.375rem]">
                <Sparkles className="w-[0.9em] h-[0.9em] text-accent" />
                <span>{t('household.buffs.activeContributorsHeader')}</span>
              </div>

              <div className="flex flex-col gap-[0.375rem]">
                {buff.contributors.map((c) => (
                  <div
                    key={c.formId}
                    onClick={() => onNpcClick?.(c.formId)}
                    className="flex flex-col gap-[0.3125rem] p-[0.5rem] bg-panel-raised rounded-md border border-border-subtle hover:border-border-accent hover:bg-interactive-hover transition-colors cursor-pointer"
                  >
                    {/* Top Row: Spouse Name + FormID + Net Multiplier Badge */}
                    <div className="flex items-center justify-between gap-[0.5rem]">
                      <div className="flex items-center gap-[0.375rem] min-w-0">
                        <User className="w-[1em] h-[1em] text-accent flex-shrink-0" />
                        <span className="text-xs font-semibold text-foreground-primary truncate">
                          {c.name}
                        </span>
                        <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-divider flex-shrink-0">
                          {c.formId}
                        </span>
                      </div>

                      <div className="flex items-center gap-[0.25rem] flex-shrink-0">
                        <span className="text-xs font-mono font-bold text-accent-bright bg-accent-soft/70 px-[0.375rem] py-[0.0625rem] rounded border border-border-accent">
                          +{c.multiplier.toFixed(2)}x
                        </span>
                      </div>
                    </div>

                    {/* Bottom Row: Breakdown badges on left, Active Follower badge on right under multiplier */}
                    <div className="flex items-center justify-between gap-[0.5rem] text-[0.6875rem]">
                      <div className="flex items-center gap-[0.375rem] flex-wrap">
                        {c.rank !== undefined && (
                          <span className="inline-flex items-center gap-[0.25rem] bg-panel-inset px-[0.375rem] py-[0.0625rem] rounded border border-border-divider">
                            <span className="text-foreground-muted">{t('household.buffs.rankLabel')}</span>
                            <span className="text-accent font-medium font-mono">
                              {getRankName(c.rank)} (x{c.rankMultiplier?.toFixed(2)})
                            </span>
                          </span>
                        )}
                        <span className="inline-flex items-center gap-[0.25rem] bg-panel-inset px-[0.375rem] py-[0.0625rem] rounded border border-border-divider">
                          <span className="text-foreground-muted">{t('household.buffs.affectionLabel')}</span>
                          <span className="text-foreground-primary font-medium font-mono">
                            {Math.round(c.affection)}/100 (x{c.affectionMultiplier.toFixed(2)})
                          </span>
                        </span>
                      </div>

                      {c.isFollower && (
                        <span className="inline-flex items-center text-[0.625rem] text-status-success bg-status-success-soft px-[0.375rem] py-[0.0625rem] rounded border border-status-success/30 font-medium flex-shrink-0">
                          {t('household.buffs.activeFollowerBadge')}
                        </span>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Potential Spouses (Married but not following) */}
          {hasPotentialSpouses && (
            <div className="space-y-[0.375rem] pt-[0.25rem]">
              <div className="text-[0.6875rem] uppercase font-semibold text-foreground-muted tracking-wider flex items-center gap-[0.375rem]">
                <UserPlus className="w-[0.9em] h-[0.9em] text-foreground-secondary" />
                <span>{t('household.buffs.availableAtHomeHeader')}</span>
              </div>

              <div className="flex flex-col gap-[0.375rem]">
                {buff.potentialSpouses?.map((p) => (
                  <div
                    key={p.formId}
                    onClick={() => onNpcClick?.(p.formId)}
                    className="flex flex-col gap-[0.3125rem] p-[0.5rem] bg-panel-inset rounded-md border border-border-divider hover:border-border-subtle transition-colors cursor-pointer opacity-85"
                  >
                    {/* Top Row: Spouse Name + FormID + Status */}
                    <div className="flex items-center justify-between gap-[0.5rem]">
                      <div className="flex items-center gap-[0.375rem] min-w-0">
                        <User className="w-[1em] h-[1em] text-foreground-muted flex-shrink-0" />
                        <span className="text-xs font-semibold text-foreground-secondary truncate">
                          {p.name}
                        </span>
                        <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-raised px-[0.25rem] py-[0.0625rem] rounded border border-border-divider flex-shrink-0">
                          {p.formId}
                        </span>
                      </div>

                      <span className="text-[0.625rem] px-[0.375rem] py-[0.0625rem] rounded bg-panel-raised border border-border-divider text-foreground-muted font-medium flex-shrink-0">
                        {t('household.buffs.atHomeNotFollowing')}
                      </span>
                    </div>

                    {/* Bottom Row: Detail badges */}
                    <div className="flex items-center gap-[0.375rem] text-[0.6875rem] flex-wrap">
                      <span className="inline-flex items-center gap-[0.25rem] bg-panel-raised px-[0.375rem] py-[0.0625rem] rounded border border-border-divider">
                        <span className="text-foreground-muted">{t('household.buffs.affectionLabel')}</span>
                        <span className="text-foreground-primary font-medium font-mono">
                          {Math.round(p.affection)}/100
                        </span>
                      </span>
                      <span className="inline-flex items-center gap-[0.25rem] bg-accent-soft/30 px-[0.375rem] py-[0.0625rem] rounded border border-border-accent/40 text-accent-bright font-mono">
                        {t('household.buffs.potentialMultiplier', { multiplier: p.affectionMultiplier.toFixed(2) })}
                      </span>
                      <span className="text-foreground-muted text-[0.625rem] italic">
                        {t('household.buffs.recruitHint')}
                      </span>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
