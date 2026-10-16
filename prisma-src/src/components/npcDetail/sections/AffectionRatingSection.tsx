import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { SliderInput } from '../../shared/SliderInput'
import { Heart } from 'lucide-react'

interface AffectionRatingSectionProps {
  npcId: string
  className?: string
}

export const AffectionRatingSection: React.FC<AffectionRatingSectionProps> = ({
  npcId,
  className = '',
}) => {
  const affectionData = useMarasStore((s) => s.affectionData)
  const updateAffection = useMarasStore((s) => s.updateAffection)

  const affectionObj = affectionData.get(npcId)
  const currentAffection = affectionObj?.permanent ?? 0

  return (
    <SectionCard
      title="Affection Rating"
      icon={Heart}
      subtitle={`${currentAffection}%`}
      className={className}
    >
      <div className="space-y-[0.5rem] pt-[0.25rem] flex flex-col justify-between h-full">
        <SliderInput
          value={currentAffection}
          min={0}
          max={100}
          unit="%"
          onChange={(val) => updateAffection(npcId, val)}
          className="w-full"
        />
        <div className="flex items-center justify-between text-[0.6875rem] text-foreground-muted pt-[0.25rem]">
          <span>Decay Mult: {affectionObj?.decayMultiplier ?? 1.0}x</span>
          <span>
            Last Interaction:{' '}
            {affectionObj?.daysSinceLastInteraction != null
              ? Math.round(affectionObj.daysSinceLastInteraction) === 0
                ? 'Today'
                : `${Math.round(affectionObj.daysSinceLastInteraction)} days ago`
              : 'Unknown'}
          </span>
        </div>
      </div>
    </SectionCard>
  )
}
