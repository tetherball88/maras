import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { GeneralSettings } from '../settings/GeneralSettings'
import { ConfirmationSettings } from '../settings/ConfirmationSettings'
import { MatchmakingSettings } from '../settings/MatchmakingSettings'
import { PartnerAgencySettings } from '../settings/PartnerAgencySettings'
import { ScenesSettings } from '../settings/ScenesSettings'
import { ChildrenSettings } from '../settings/ChildrenSettings'
import { FertilitySettings } from '../settings/FertilitySettings'
import { AIDialogueSettings } from '../settings/AIDialogueSettings'
import { SkyrimNetSettings } from '../settings/SkyrimNetSettings'
import { CheatsSettings } from '../settings/CheatsSettings'
import { ScrollArea } from '@/components/ui/scroll-area'

export const SettingsPage: React.FC = () => {
  const activeSubSection = useMarasStore((s) => s.activeSubSection)
  const uiFullScreen = useMarasStore((s) => Boolean(s.settings.uiFullScreen))

  const renderSubSection = () => {
    switch (activeSubSection) {
      case 'general':
        return <GeneralSettings />
      case 'confirmations':
        return <ConfirmationSettings />
      case 'matchmaking':
        return <MatchmakingSettings />
      case 'partnerAgency':
        return <PartnerAgencySettings />
      case 'scenes':
        return <ScenesSettings />
      case 'children':
        return <ChildrenSettings />
      case 'fertility':
        return <FertilitySettings />
      case 'aiDialogue':
        return <AIDialogueSettings />
      case 'skyrimNet':
        return <SkyrimNetSettings />
      case 'cheats':
        return <CheatsSettings />
      default:
        return <GeneralSettings />
    }
  }

  return (
    <ScrollArea className="h-full w-full">
      <div className={`${uiFullScreen ? 'max-w-[60rem]' : 'max-w-[44rem]'} mx-auto p-[0.5rem] pr-[1rem] pb-[2rem]`}>
        {renderSubSection()}
      </div>
    </ScrollArea>
  )
}

