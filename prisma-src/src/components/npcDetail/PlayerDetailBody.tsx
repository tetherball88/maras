import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { categorizePartners } from '../../utils/partnerCategories'
import {
  PlayerPolicySection,
  HouseholdChildrenSection,
  TrackedRelationshipsSection,
} from './sections'

export const PlayerDetailBody: React.FC = () => {
  const relationships = useMarasStore((s) => s.relationships)
  const cheatingRecords = useMarasStore((s) => s.cheatingRecords)
  const children = useMarasStore((s) => s.children)
  const settings = useMarasStore((s) => s.settings)

  const categorized = categorizePartners(PLAYER_FORM_ID, relationships, cheatingRecords, settings.enablePolygamy)

  const hasChildren = Array.from(children.values()).some(
    (c) =>
      c.guardianAId === PLAYER_FORM_ID ||
      c.guardianBId === PLAYER_FORM_ID ||
      c.bioMotherId === PLAYER_FORM_ID ||
      c.bioFatherId === PLAYER_FORM_ID,
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
      {/* Player Polygamy & Relationship Policy Card */}
      <PlayerPolicySection />

      {/* Children and Tracked Relationships (Side-by-side if both present, full width if only one present) */}
      {hasChildren && hasRelationships ? (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-[1rem] items-start">
          <HouseholdChildrenSection npcId={PLAYER_FORM_ID} className="h-full" />
          <TrackedRelationshipsSection npcId={PLAYER_FORM_ID} className="h-full" />
        </div>
      ) : (
        <>
          {hasChildren && <HouseholdChildrenSection npcId={PLAYER_FORM_ID} />}
          {hasRelationships && <TrackedRelationshipsSection npcId={PLAYER_FORM_ID} />}
        </>
      )}
    </div>
  )
}
