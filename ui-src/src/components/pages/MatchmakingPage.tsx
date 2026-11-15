import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { NpcPickerPanel } from '../matchmaking/NpcPickerPanel'
import { StatusAssignment } from '../matchmaking/StatusAssignment'
import { LlmMatchmakingSubPage } from '../matchmaking/LlmMatchmakingSubPage'
import { AdoptionSubPage } from '../matchmaking/AdoptionSubPage'

export const MatchmakingPage: React.FC = () => {
  const activeSubSection = useMarasStore((s) => s.activeSubSection)
  const settings = useMarasStore((s) => s.settings)

  if (activeSubSection === 'adoption') {
    return <AdoptionSubPage />
  }

  if (activeSubSection === 'llmMatch' && settings.hasSkyrimNet) {
    return <LlmMatchmakingSubPage />
  }

  return (
    <div className="flex h-full w-full gap-[1rem] overflow-hidden">
      <NpcPickerPanel side="a" title="Select NPC A" />
      <StatusAssignment />
      <NpcPickerPanel side="b" title="Select NPC B" />
    </div>
  )
}


