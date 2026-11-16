import React from 'react'
import { Card, CardHeader, CardTitle, CardDescription, CardContent } from '../ui/card'
import { Sparkles } from 'lucide-react'
import { useTranslation } from '../../i18n'

interface PagePlaceholderProps {
  title: string
  phaseNumber: number
}

export const PagePlaceholder: React.FC<PagePlaceholderProps> = ({ title, phaseNumber }) => {
  const { t } = useTranslation()

  return (
    <div className="flex-1 flex items-center justify-center p-[2rem]">
      <Card className="max-w-[28rem] w-full bg-panel-raised border-border-strong text-center p-[1rem]">
        <CardHeader className="flex flex-col items-center gap-[0.5rem]">
          <div className="p-[0.75rem] rounded-full bg-accent-soft text-accent">
            <Sparkles className="w-[1.5em] h-[1.5em]" />
          </div>
          <CardTitle className="text-xl text-accent-bright">{title}</CardTitle>
          <CardDescription className="text-foreground-secondary">
            {t('shared.placeholder.scheduled', { phase: phaseNumber })}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <p className="text-sm text-foreground-muted">
            {t('shared.placeholder.description', { phase: phaseNumber })}
          </p>
        </CardContent>
      </Card>
    </div>
  )
}
