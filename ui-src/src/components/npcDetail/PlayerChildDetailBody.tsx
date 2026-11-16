import React from 'react'
import { useTranslation } from '../../i18n'
import { ChildDetailBody } from './ChildDetailBody'
import { ChildCritterPetSection } from './sections/ChildCritterPetSection'

interface PlayerChildDetailBodyProps {
  npcId: string
  displayName: string
  displayRace: string
}

export const PlayerChildDetailBody: React.FC<PlayerChildDetailBodyProps> = ({
  npcId,
  displayName,
  displayRace,
}) => {
  const { t, tp } = useTranslation()
  void t
  void tp
  return (
    <div className="max-w-2xl mx-auto space-y-[1.25rem]">
      {/* Base Child Details */}
      <ChildDetailBody
        npcId={npcId}
        displayName={displayName}
        displayRace={displayRace}
        isPlayerChild={true}
      />

      {/* Critter & Pet Management (Physical child actors only) */}
      <ChildCritterPetSection npcId={npcId} />
    </div>
  )
}
