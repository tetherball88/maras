import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { NpcPickerPanel } from '../matchmaking/NpcPickerPanel'
import { StatusAssignment } from '../matchmaking/StatusAssignment'
import { LlmMatchmakingSubPage } from '../matchmaking/LlmMatchmakingSubPage'
import { AdoptionSubPage } from '../matchmaking/AdoptionSubPage'
import { useTranslation } from '../../i18n'

export const MatchmakingPage: React.FC = () => {
  const { t } = useTranslation()
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
      <NpcPickerPanel side="a" title={t('matchmaking.manual.selectNpcA')} />
      <StatusAssignment />
      <NpcPickerPanel side="b" title={t('matchmaking.manual.selectNpcB')} />
    </div>
  )
}


