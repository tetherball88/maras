import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { NpcPartnerGroup } from '../NpcPartnerGroup'
import { categorizePartners } from '../../../utils/partnerCategories'
import { Users, Heart, Shield, Flame, AlertTriangle, Clock } from 'lucide-react'

interface TrackedRelationshipsSectionProps {
  npcId: string
  className?: string
}

export const TrackedRelationshipsSection: React.FC<TrackedRelationshipsSectionProps> = ({
  npcId,
  className = '',
}) => {
  const relationships = useMarasStore((s) => s.relationships)
  const cheatingRecords = useMarasStore((s) => s.cheatingRecords)
  const settings = useMarasStore((s) => s.settings)
  const setPartnerId = useMarasStore((s) => s.setNpcDetailPartnerId)

  const categorized = categorizePartners(npcId, relationships, cheatingRecords, settings.enablePolygamy)

  const hasRelationships = Boolean(
    categorized &&
    (categorized.primary.length > 0 ||
      categorized.openRelationship.length > 0 ||
      categorized.lovers.length > 0 ||
      categorized.cheating.length > 0 ||
      categorized.candidate.length > 0 ||
      categorized.former.length > 0),
  )

  if (!hasRelationships || !categorized) return null

  return (
    <SectionCard title="Tracked Relationships & Partners" icon={Users} className={className}>
      <div className="space-y-[0.75rem] pt-[0.25rem] max-h-[22rem] overflow-y-auto pr-[0.25rem]">
        {/* Primary Partners (Married/Engaged/Dating) */}
        <NpcPartnerGroup
          title="Primary Partners (Spouse / Engaged / Dating)"
          icon={Heart}
          badgeColor="text-accent"
          relationships={categorized.primary}
          currentNpcId={npcId}
          onSelectPartner={(pid) => setPartnerId(pid)}
        />

        {/* Open Relationship Partners */}
        <NpcPartnerGroup
          title="Open Relationship Partners"
          icon={Shield}
          badgeColor="text-status-success"
          relationships={categorized.openRelationship}
          currentNpcId={npcId}
          onSelectPartner={(pid) => setPartnerId(pid)}
        />

        {/* Lovers */}
        <NpcPartnerGroup
          title="Lovers"
          icon={Flame}
          badgeColor="text-pink-400"
          relationships={categorized.lovers}
          currentNpcId={npcId}
          onSelectPartner={(pid) => setPartnerId(pid)}
        />

        {/* Cheating Partners */}
        <NpcPartnerGroup
          title="Cheating / Affairs"
          icon={AlertTriangle}
          badgeColor="text-status-danger"
          relationships={categorized.cheating}
          currentNpcId={npcId}
          onSelectPartner={(pid) => setPartnerId(pid)}
        />

        {/* Candidate Partners */}
        <NpcPartnerGroup
          title="Prospective Candidates"
          icon={Flame}
          badgeColor="text-foreground-muted"
          relationships={categorized.candidate}
          currentNpcId={npcId}
          onSelectPartner={(pid) => setPartnerId(pid)}
        />

        {/* Former Partners */}
        <NpcPartnerGroup
          title="Former Relationships (Exes)"
          icon={Clock}
          badgeColor="text-foreground-muted"
          relationships={categorized.former}
          currentNpcId={npcId}
          onSelectPartner={(pid) => setPartnerId(pid)}
        />
      </div>
    </SectionCard>
  )
}
