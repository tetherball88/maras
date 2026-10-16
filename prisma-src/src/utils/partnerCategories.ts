import { RelationshipPair, RelationshipStatus, AffairStatus, CheatingRecord } from '../types'
import { PLAYER_FORM_ID } from '../data/mockData'

export type PartnerCategory =
  | 'primary'
  | 'openRelationship'
  | 'lovers'
  | 'cheating'
  | 'candidate'
  | 'former'

export interface CategorizedPartners {
  primary: RelationshipPair[]
  openRelationship: RelationshipPair[]
  lovers: RelationshipPair[]
  cheating: RelationshipPair[]
  candidate: RelationshipPair[]
  former: RelationshipPair[]
}

export function categorizePartners(
  selectedNpcId: string,
  relationships: RelationshipPair[],
  cheatingRecords: CheatingRecord[] = [],
  enablePolygamy: boolean = false,
): CategorizedPartners {
  const isPlayer = selectedNpcId === PLAYER_FORM_ID

  const isValidActorId = (id?: string | null): id is string => {
    return Boolean(id && id !== '' && id !== '00000000')
  }

  // Find all active affairs where selectedNpcId is the cheater
  const activeAffairPartnerIds = new Set<string>()
  for (const cr of cheatingRecords) {
    const isOngoing = cr.affairStatus === undefined || cr.affairStatus === AffairStatus.Ongoing
    if (!isOngoing) continue

    if (cr.cheaterId === selectedNpcId && isValidActorId(cr.affairPartnerId) && cr.affairPartnerId !== selectedNpcId) {
      activeAffairPartnerIds.add(cr.affairPartnerId)
    }
  }

  // Find all existing relationships directly involving selectedNpcId
  const rels = relationships.filter((r) => {
    if (!isValidActorId(r.actorA) || !isValidActorId(r.actorB)) return false
    if (r.actorA === r.actorB) return false
    return r.actorA === selectedNpcId || r.actorB === selectedNpcId
  })

  const existingPartnerIds = new Set(
    rels.map((r) => (r.actorA === selectedNpcId ? r.actorB : r.actorA)),
  )

  const allRels: RelationshipPair[] = [...rels]

  // Add affair partners who don't have an official relationship pair
  for (const partnerId of activeAffairPartnerIds) {
    if (!existingPartnerIds.has(partnerId)) {
      allRels.push({
        actorA: selectedNpcId,
        actorB: partnerId,
        status: RelationshipStatus.Candidate,
        isOpen: false,
        statusHistory: [],
      })
      existingPartnerIds.add(partnerId)
    }
  }

  const primary: RelationshipPair[] = []
  const openRelPartners: RelationshipPair[] = []
  const lovers: RelationshipPair[] = []
  const cheatingPartners: RelationshipPair[] = []
  const candidates: RelationshipPair[] = []
  const formers: RelationshipPair[] = []

  for (const r of allRels) {
    const partnerId = r.actorA === selectedNpcId ? r.actorB : r.actorA
    if (!isValidActorId(partnerId) || partnerId === selectedNpcId) {
      continue
    }

    const isAffair = activeAffairPartnerIds.has(partnerId) && !(isPlayer && enablePolygamy)

    if (
      r.status === RelationshipStatus.Divorced ||
      r.status === RelationshipStatus.Jilted ||
      r.status === RelationshipStatus.ExDating ||
      r.status === RelationshipStatus.ExLover
    ) {
      formers.push(r)
    } else if (r.status === RelationshipStatus.Married || r.status === RelationshipStatus.Engaged) {
      primary.push(r)
    } else if (isAffair) {
      cheatingPartners.push(r)
    } else if (r.isOpen) {
      openRelPartners.push(r)
    } else if (r.status === RelationshipStatus.Dating) {
      primary.push(r)
    } else if (r.status === RelationshipStatus.Lover) {
      lovers.push(r)
    } else if (r.status === RelationshipStatus.Candidate) {
      candidates.push(r)
    }
  }

  return {
    primary,
    openRelationship: openRelPartners,
    lovers,
    cheating: cheatingPartners,
    candidate: candidates,
    former: formers,
  }
}
