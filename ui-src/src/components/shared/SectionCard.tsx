import React from 'react'
import { Card, CardHeader, CardTitle, CardContent } from '@/components/ui/card'
import { LucideIcon } from 'lucide-react'

interface SectionCardProps {
  title: string
  icon?: LucideIcon
  subtitle?: string
  actionSlot?: React.ReactNode
  children: React.ReactNode
  className?: string
}

export const SectionCard: React.FC<SectionCardProps> = ({
  title,
  icon: Icon,
  subtitle,
  actionSlot,
  children,
  className = '',
}) => {
  return (
    <Card className={`bg-panel-raised border-border-subtle p-[0.75rem] space-y-[0.5rem] ${className}`}>
      <CardHeader className="p-0 flex flex-row items-center justify-between border-b border-border-divider pb-[0.375rem] space-y-0">
        <CardTitle className="text-xs font-semibold text-accent-bright flex items-center gap-[0.375rem]">
          {Icon && <Icon className="w-[1.125em] h-[1.125em] text-accent" />}
          <span>{title}</span>
        </CardTitle>
        {actionSlot}
        {subtitle && !actionSlot && (
          <span className="text-xs font-semibold text-accent">{subtitle}</span>
        )}
      </CardHeader>

      <CardContent className="p-0 pt-[0.25rem] text-xs">{children}</CardContent>
    </Card>
  )
}
