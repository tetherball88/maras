import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { areSameFormId } from '../../utils/statusAvailability'
import {
  ChildProfileResidenceSection,
  ChildGuardiansSection,
  AiPackageSection,
} from './sections'

interface ChildDetailBodyProps {
  npcId: string
  displayName: string
  displayRace: string
  isPlayerChild?: boolean
}

export const ChildDetailBody: React.FC<ChildDetailBodyProps> = ({
  npcId,
  displayName,
  displayRace,
  isPlayerChild,
}) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const children = useMarasStore((s) => s.children)

  const npc = npcProfiles.get(npcId)
  const childRecord = children.get(npcId)
  const hasRefID = childRecord?.hasRefID ?? npc?.hasRefID ?? !npcId.startsWith('FF7')

  return (
    <div className="max-w-2xl mx-auto space-y-[1.25rem]">
      <ChildProfileResidenceSection
        npcId={npcId}
        displayName={displayName}
        displayRace={displayRace}
        isPlayerChild={isPlayerChild}
      />
      <ChildGuardiansSection npcId={npcId} />
      {hasRefID && <AiPackageSection npcId={npcId} />}
    </div>
  )
}
