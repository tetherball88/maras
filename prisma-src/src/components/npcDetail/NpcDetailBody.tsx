import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { categorizePartners } from '../../utils/partnerCategories'
import {
  ProfileCharacteristicsSection,
  AiPackageSection,
  HouseholdChildrenSection,
  TrackedRelationshipsSection,
} from './sections'

interface NpcDetailBodyProps {
  npcId: string
}

export const NpcDetailBody: React.FC<NpcDetailBodyProps> = ({ npcId }) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const cheatingRecords = useMarasStore((s) => s.cheatingRecords)
  const children = useMarasStore((s) => s.children)
  const settings = useMarasStore((s) => s.settings)

  const npc = npcProfiles.get(npcId)
  const categorized = npc ? categorizePartners(npcId, relationships, cheatingRecords, settings.enablePolygamy) : null

  const hasChildren = Array.from(children.values()).some(
    (c) =>
      c.guardianAId === npcId ||
      c.guardianBId === npcId ||
      c.bioMotherId === npcId ||
      c.bioFatherId === npcId,
  )

  const hasRelationships = Boolean(
    categorized &&
    (categorized.primary.length > 0 ||
      categorized.openRelationship.length > 0 ||
      categorized.lovers.length > 0 ||
      categorized.cheating.length > 0 ||
      categorized.candidate.length > 0 ||
      categorized.former.length > 0),
  )

  return (
    <div className="max-w-4xl mx-auto space-y-[1.25rem]">
      {/* Profile Characteristics & AI Process Package (Side-by-side row) */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-[1rem] items-stretch">
        <ProfileCharacteristicsSection npcId={npcId} className="h-full lg:col-span-2" />
        <AiPackageSection npcId={npcId} className="h-full lg:col-span-1" />
      </div>

      {/* Children and Tracked Relationships (Side-by-side if both present, full width if only one present) */}
      {hasChildren && hasRelationships ? (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-[1rem] items-start">
          <HouseholdChildrenSection npcId={npcId} className="h-full" />
          <TrackedRelationshipsSection npcId={npcId} className="h-full" />
        </div>
      ) : (
        <>
          {hasChildren && <HouseholdChildrenSection npcId={npcId} />}
          {hasRelationships && <TrackedRelationshipsSection npcId={npcId} />}
        </>
      )}
    </div>
  )
}
