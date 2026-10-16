import React from 'react'
import { RelationshipStatus } from '../../types'
import { Badge } from '../ui/badge'
import { AppTooltip } from './AppTooltip'
import { cn } from '@/lib/utils'

interface StatusBadgeProps {
  status: RelationshipStatus
  className?: string
}

export const getStatusLabel = (status: RelationshipStatus): string => {
  switch (status) {
    case RelationshipStatus.Candidate:
      return 'Candidate'
    case RelationshipStatus.Dating:
      return 'Dating'
    case RelationshipStatus.Lover:
      return 'Lover'
    case RelationshipStatus.Engaged:
      return 'Engaged'
    case RelationshipStatus.Married:
      return 'Married'
    case RelationshipStatus.Divorced:
      return 'Divorced'
    case RelationshipStatus.Jilted:
      return 'Jilted'
    case RelationshipStatus.ExDating:
      return 'Ex-Dating'
    case RelationshipStatus.ExLover:
      return 'Ex-Lover'
    default:
      return 'Unknown'
  }
}

export const getStatusIconUrl = (status: RelationshipStatus): string => {
  switch (status) {
    case RelationshipStatus.Married:
      return './assets/icons/married.png'
    case RelationshipStatus.Engaged:
      return './assets/icons/engaged.png'
    case RelationshipStatus.Lover:
      return './assets/icons/lovers.png'
    case RelationshipStatus.Dating:
      return './assets/icons/dating.png'
    case RelationshipStatus.Divorced:
      return './assets/icons/divorced.png'
    case RelationshipStatus.Jilted:
      return './assets/icons/jilted.png'
    case RelationshipStatus.ExDating:
      return './assets/icons/exdating.png'
    case RelationshipStatus.ExLover:
      return './assets/icons/exlover.png'
    case RelationshipStatus.Candidate:
    default:
      return './assets/icons/candidate.png'
  }
}

export const getStatusBorderColorClass = (status: RelationshipStatus): string => {
  switch (status) {
    case RelationshipStatus.Married:
      return 'border-status-success'
    case RelationshipStatus.Dating:
    case RelationshipStatus.Lover:
      return 'border-border-accent'
    case RelationshipStatus.Engaged:
      return 'border-status-warning'
    case RelationshipStatus.Divorced:
    case RelationshipStatus.Jilted:
      return 'border-status-danger'
    case RelationshipStatus.Candidate:
    case RelationshipStatus.ExDating:
    case RelationshipStatus.ExLover:
    default:
      return 'border-border-subtle'
  }
}

export const getStatusIcon = (status: RelationshipStatus): string => {
  switch (status) {
    case RelationshipStatus.Married:
      return '💍'
    case RelationshipStatus.Engaged:
      return '💖'
    case RelationshipStatus.Lover:
      return '💕'
    case RelationshipStatus.Dating:
      return '💑'
    case RelationshipStatus.Divorced:
      return '💔'
    case RelationshipStatus.Jilted:
      return '🥀'
    case RelationshipStatus.ExDating:
    case RelationshipStatus.ExLover:
      return '🌧️'
    default:
      return '✨'
  }
}

export interface StatusIconProps extends React.ImgHTMLAttributes<HTMLImageElement> {
  status: RelationshipStatus
  size?: string
  showTooltip?: boolean
  className?: string
}

export const StatusIcon = React.forwardRef<HTMLImageElement, StatusIconProps>(
  ({ status, size = '2em', showTooltip = true, className, style, alt, ...props }, ref) => {
    const label = getStatusLabel(status)
    const iconUrl = getStatusIconUrl(status)
    const borderColorClass = getStatusBorderColorClass(status)

    const imgNode = (
      <img
        ref={ref}
        src={iconUrl}
        alt={alt || label}
        style={{ width: size, height: size, ...style }}
        className={cn(
          'rounded-full border-2 border-solid object-cover inline-block flex-shrink-0 align-middle',
          borderColorClass,
          className,
        )}
        {...props}
      />
    )

    if (showTooltip) {
      return <AppTooltip content={`Status: ${label}`}>{imgNode}</AppTooltip>
    }

    return imgNode
  },
)
StatusIcon.displayName = 'StatusIcon'

export const StatusBadge: React.FC<StatusBadgeProps> = ({ status, className = '' }) => {
  const label = getStatusLabel(status)

  let badgeVariant: 'default' | 'secondary' | 'outline' | 'success' | 'danger' | 'warning' = 'secondary'

  if (status === RelationshipStatus.Married) {
    badgeVariant = 'success'
  } else if (status === RelationshipStatus.Dating || status === RelationshipStatus.Lover) {
    badgeVariant = 'default'
  } else if (status === RelationshipStatus.Engaged) {
    badgeVariant = 'warning'
  } else if (status === RelationshipStatus.Divorced || status === RelationshipStatus.Jilted) {
    badgeVariant = 'danger'
  } else {
    badgeVariant = 'outline'
  }

  return (
    <StatusIcon status={status} size="2em" showTooltip />
  )
}


