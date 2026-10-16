import React from 'react'
import { SocialClass, SkillType, Temperament, NPCProfile } from '../../types'
import { cn } from '@/lib/utils'
import { AppTooltip } from './AppTooltip'

// ==========================================
// Generic Parsing & Label Helpers
// ==========================================

function parseTraitEnum<T extends number>(
  val: any,
  enumObj: Record<string, string | number>,
  defaultValue: T,
): T {
  if (val === undefined || val === null) return defaultValue
  if (typeof val === 'number') return val in enumObj ? (val as T) : defaultValue
  if (!isNaN(Number(val))) {
    const num = Number(val)
    return num in enumObj ? (num as T) : defaultValue
  }
  const lower = String(val).toLowerCase()
  const keys = Object.keys(enumObj).filter((k) => isNaN(Number(k)))
  const match = keys.find((k) => k.toLowerCase() === lower || lower.includes(k.toLowerCase()))
  return match !== undefined ? (enumObj[match] as T) : defaultValue
}

export const parseSocialClass = (val: any): SocialClass =>
  parseTraitEnum(val, SocialClass, SocialClass.Middle)

export const parseSkillType = (val: any): SkillType =>
  parseTraitEnum(val, SkillType, SkillType.Warrior)

export const parseTemperament = (val: any): Temperament =>
  parseTraitEnum(val, Temperament, Temperament.Proud)

export const getSocialClassLabel = (sc: any): string =>
  SocialClass[parseSocialClass(sc)] || 'Middle'

export const getSkillTypeLabel = (st: any): string =>
  SkillType[parseSkillType(st)] || 'Warrior'

export const getTemperamentLabel = (t: any): string =>
  Temperament[parseTemperament(t)] || 'Proud'

// ==========================================
// Icon URL Resolvers
// ==========================================

const CUSTOM_ICON_NAMES: Record<string, string> = {
  nobles: 'noble',
  rulers: 'ruler',
  working: 'middle',
}

function getTraitIconUrl(
  category: 'socialClass' | 'skillType' | 'temperament',
  val: any,
  variant: 'default' | 'square' = 'default',
): string {
  const dir = variant === 'square' ? './assets/icons/squars' : './assets/icons'
  let rawName = ''

  if (category === 'socialClass') {
    rawName = SocialClass[parseSocialClass(val)]
  } else if (category === 'skillType') {
    rawName = SkillType[parseSkillType(val)]
  } else {
    rawName = Temperament[parseTemperament(val)]
  }

  const key = (rawName || '').toLowerCase()
  const filename = CUSTOM_ICON_NAMES[key] || key
  return `${dir}/${filename}.png`
}

export const getSocialClassIconUrl = (sc: any, variant?: 'default' | 'square') =>
  getTraitIconUrl('socialClass', sc, variant)

export const getSkillTypeIconUrl = (st: any, variant?: 'default' | 'square') =>
  getTraitIconUrl('skillType', st, variant)

export const getTemperamentIconUrl = (t: any, variant?: 'default' | 'square') =>
  getTraitIconUrl('temperament', t, variant)

function resolveSize(size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl' | string): string {
  if (!size) return '1em'
  switch (size) {
    case 'xs':
      return '0.875em'
    case 'sm':
      return '1em'
    case 'md':
      return '1.25em'
    case 'lg':
      return '1.5em'
    case 'xl':
      return '2em'
    default:
      return size
  }
}

// ==========================================
// Base Component Interfaces & Primitives
// ==========================================

export interface BaseTraitIconProps extends React.ImgHTMLAttributes<HTMLImageElement> {
  size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl' | string
  variant?: 'default' | 'square'
  showLabel?: boolean
  showTooltip?: boolean
  className?: string
}

export interface SocialClassIconProps extends BaseTraitIconProps {
  value: SocialClass | number | string
}

export interface SkillTypeIconProps extends BaseTraitIconProps {
  value: SkillType | number | string
}

export interface TemperamentIconProps extends BaseTraitIconProps {
  value: Temperament | number | string
}

export interface TraitIconBaseProps extends React.ImgHTMLAttributes<HTMLImageElement> {
  iconUrl: string
  label: string
  category: string
  size?: 'sm' | 'md' | 'lg' | string
  showLabel?: boolean
  showTooltip?: boolean
  className?: string
  iconClassName?: string
}

const TraitIconBase = React.forwardRef<HTMLImageElement, TraitIconBaseProps>(
  (
    {
      iconUrl,
      label,
      category,
      size = 'md',
      showLabel = false,
      showTooltip = true,
      className,
      style,
      alt,
      ...props
    },
    ref,
  ) => {
    const dim = resolveSize(size)

    const imgNode = (
      <img
        ref={ref}
        src={iconUrl}
        alt={alt || label}
        style={{ width: dim, height: dim, ...style }}
        className={cn('object-contain inline-block flex-shrink-0 align-middle', className)}
        {...props}
      />
    )

    const contentNode = showLabel ? (
      <span className="inline-flex items-center gap-[0.375em] text-xs font-medium text-foreground-primary">
        {imgNode}
        <span>{label}</span>
      </span>
    ) : (
      imgNode
    )

    if (showTooltip) {
      return <AppTooltip content={`${category}: ${label}`}>{contentNode}</AppTooltip>
    }

    return contentNode
  },
)
TraitIconBase.displayName = 'TraitIconBase'

export interface TraitBadgeProps {
  size?: 'sm' | 'md' | 'lg'
  variant?: 'default' | 'square'
  className?: string
  iconClassName?: string
  showIconOnly?: boolean
}

export function TraitBadgeBase({
  category,
  label,
  icon,
  size = 'md',
  showIconOnly = false,
  className,
}: TraitBadgeProps & { category: string; label: string; icon: React.ReactNode }) {
  const sizeClasses =
    size === 'sm'
      ? showIconOnly
        ? 'p-[0.125rem]'
        : 'text-[0.6875rem] px-[0.375rem] py-[0.125rem] gap-[0.25rem]'
      : size === 'md'
        ? showIconOnly
          ? 'p-[0.25rem]'
          : 'text-xs px-[0.5rem] py-[0.25rem] gap-[0.375rem]'
        : showIconOnly
          ? 'p-[0.5rem]'
          : 'text-sm px-[0.75rem] py-[0.375rem] gap-[0.5rem]'

  const badgeNode = (
    <span
      className={cn(
        'inline-flex items-center rounded-md font-medium bg-panel-inset border border-border-subtle text-foreground-secondary hover:border-border-strong transition-colors cursor-pointer',
        sizeClasses,
        className,
      )}
    >
      {icon}
      {!showIconOnly && <span>{label}</span>}
    </span>
  )

  return <AppTooltip content={`${category}: ${label}`}>{badgeNode}</AppTooltip>
}

export const TraitBadge = TraitBadgeBase

// ==========================================
// Public Icon Components
// ==========================================

export const SocialClassIcon = React.forwardRef<HTMLImageElement, SocialClassIconProps>(
  (props, ref) => {
    const sc = parseSocialClass(props.value)
    return (
      <TraitIconBase
        ref={ref}
        category="Social Class"
        label={getSocialClassLabel(sc)}
        iconUrl={getSocialClassIconUrl(sc, props.variant)}
        {...props}
      />
    )
  },
)
SocialClassIcon.displayName = 'SocialClassIcon'

export const SkillTypeIcon = React.forwardRef<HTMLImageElement, SkillTypeIconProps>(
  (props, ref) => {
    const st = parseSkillType(props.value)
    return (
      <TraitIconBase
        ref={ref}
        category="Skill Class"
        label={getSkillTypeLabel(st)}
        iconUrl={getSkillTypeIconUrl(st, props.variant)}
        {...props}
      />
    )
  },
)
SkillTypeIcon.displayName = 'SkillTypeIcon'

export const TemperamentIcon = React.forwardRef<HTMLImageElement, TemperamentIconProps>(
  (props, ref) => {
    const tp = parseTemperament(props.value)
    return (
      <TraitIconBase
        ref={ref}
        category="Temperament"
        label={getTemperamentLabel(tp)}
        iconUrl={getTemperamentIconUrl(tp, props.variant)}
        {...props}
      />
    )
  },
)
TemperamentIcon.displayName = 'TemperamentIcon'

// ==========================================
// Public Badge Components
// ==========================================

export interface SocialClassBadgeProps extends TraitBadgeProps {
  value: SocialClass | number | string
}

export interface SkillTypeBadgeProps extends TraitBadgeProps {
  value: SkillType | number | string
}

export interface TemperamentBadgeProps extends TraitBadgeProps {
  value: Temperament | number | string
}

export const SocialClassBadge: React.FC<SocialClassBadgeProps> = (props) => {
  const sc = parseSocialClass(props.value)
  const label = getSocialClassLabel(sc)
  const iconSize = props.size === 'sm' ? '0.875em' : props.size === 'md' ? '1.125em' : '1.375em'

  return (
    <TraitBadgeBase
      category="Social Class"
      label={label}
      icon={
        <SocialClassIcon
          value={sc}
          size={iconSize}
          variant={props.variant}
          className={props.iconClassName}
          showTooltip={false}
        />
      }
      {...props}
    />
  )
}

export const SkillTypeBadge: React.FC<SkillTypeBadgeProps> = (props) => {
  const st = parseSkillType(props.value)
  const label = getSkillTypeLabel(st)
  const iconSize = props.size === 'sm' ? '0.875em' : props.size === 'md' ? '1.125em' : '1.375em'

  return (
    <TraitBadgeBase
      category="Skill Class"
      label={label}
      icon={
        <SkillTypeIcon
          value={st}
          size={iconSize}
          variant={props.variant}
          className={props.iconClassName}
          showTooltip={false}
        />
      }
      {...props}
    />
  )
}

export const TemperamentBadge: React.FC<TemperamentBadgeProps> = (props) => {
  const tp = parseTemperament(props.value)
  const label = getTemperamentLabel(tp)
  const iconSize = props.size === 'sm' ? '0.875em' : props.size === 'md' ? '1.125em' : '1.375em'

  return (
    <TraitBadgeBase
      category="Temperament"
      label={label}
      icon={
        <TemperamentIcon
          value={tp}
          size={iconSize}
          variant={props.variant}
          className={props.iconClassName}
          showTooltip={false}
        />
      }
      {...props}
    />
  )
}

// ==========================================
// Combined NPC Trait Badges Group
// ==========================================

export interface NpcTraitBadgesProps {
  npc?: Partial<NPCProfile> | null
  socialClass?: SocialClass | number | string
  skillType?: SkillType | number | string
  temperament?: Temperament | number | string
  size?: 'sm' | 'md' | 'lg'
  variant?: 'default' | 'square'
  showIconOnly?: boolean
  className?: string
}

export const NpcTraitBadges: React.FC<NpcTraitBadgesProps> = ({
  npc,
  socialClass,
  skillType,
  temperament,
  size = 'sm',
  variant = 'default',
  showIconOnly = false,
  className,
}) => {
  const sc = npc?.socialClass !== undefined ? npc.socialClass : socialClass
  const st = npc?.skillType !== undefined ? npc.skillType : skillType
  const tp = npc?.temperament !== undefined ? npc.temperament : temperament

  return (
    <div className={cn('inline-flex items-center gap-[0.25rem] flex-wrap', className)}>
      {sc !== undefined && (
        <SocialClassBadge value={sc} size={size} variant={variant} showIconOnly={showIconOnly} />
      )}
      {st !== undefined && (
        <SkillTypeBadge value={st} size={size} variant={variant} showIconOnly={showIconOnly} />
      )}
      {tp !== undefined && (
        <TemperamentBadge value={tp} size={size} variant={variant} showIconOnly={showIconOnly} />
      )}
    </div>
  )
}
