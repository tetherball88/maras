import React from 'react'
import { Card, CardHeader, CardTitle, CardDescription, CardContent } from '../ui/card'
import { Sparkles } from 'lucide-react'

interface PagePlaceholderProps {
  title: string
  phaseNumber: number
}

export const PagePlaceholder: React.FC<PagePlaceholderProps> = ({ title, phaseNumber }) => {
  return (
    <div className="flex-1 flex items-center justify-center p-[2rem]">
      <Card className="max-w-[28rem] w-full bg-panel-raised border-border-strong text-center p-[1rem]">
        <CardHeader className="flex flex-col items-center gap-[0.5rem]">
          <div className="p-[0.75rem] rounded-full bg-accent-soft text-accent">
            <Sparkles className="w-[1.5em] h-[1.5em]" />
          </div>
          <CardTitle className="text-xl text-accent-bright">{title}</CardTitle>
          <CardDescription className="text-foreground-secondary">
            This module is scheduled for implementation in Phase {phaseNumber}.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <p className="text-sm text-foreground-muted">
            The foundation shell is active. Select another tab or proceed with Phase {phaseNumber} build out.
          </p>
        </CardContent>
      </Card>
    </div>
  )
}
