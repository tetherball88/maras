import React from 'react'
import { Progress } from '@/components/ui/progress'

interface AffectionBarProps {
  value: number // 0 - 100
  size?: 'sm' | 'md' | 'lg'
  showLabel?: boolean
  className?: string
}

export const AffectionBar: React.FC<AffectionBarProps> = ({
  value,
  size = 'md',
  showLabel = true,
  className = '',
}) => {
  const clampedValue = Math.max(0, Math.min(100, value))

  let indicatorColor = 'bg-accent'
  if (clampedValue >= 70) {
    indicatorColor = 'bg-status-success'
  } else if (clampedValue >= 40) {
    indicatorColor = 'bg-accent'
  } else {
    indicatorColor = 'bg-status-warning'
  }

  const heightClass =
    size === 'sm' ? 'h-[0.375rem]' : size === 'lg' ? 'h-[0.75rem]' : 'h-[0.5rem]'

  return (
    <div className={`flex items-center gap-[0.5rem] w-full ${className}`}>
      <Progress
        value={clampedValue}
        className={`flex-1 ${heightClass}`}
        indicatorClassName={indicatorColor}
      />
      {showLabel && (
        <span className="text-xs font-mono font-semibold text-foreground-secondary min-w-[2.25rem] text-right">
          {clampedValue}%
        </span>
      )}
    </div>
  )
}
