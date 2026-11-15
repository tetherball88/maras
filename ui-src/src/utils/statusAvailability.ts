import { useMemo } from 'react'
import { RelationshipStatus, RelationshipPair, NPCProfile } from '../types'
import { PLAYER_FORM_ID } from '../data/mockData'
import { useMarasStore } from '../store/useMarasStore'
import { getStatusLabel } from '../components/shared/StatusBadge'

export interface StatusOptionInfo {
  status: RelationshipStatus
  value: number
  label: string
  enabled: boolean
  disabled: boolean
  tooltip?: string
  isCurrent?: boolean
  loverType?: 'allowed' | 'cheating'
}

export interface StatusAvailabilityContext {
  relationships: RelationshipPair[]
  npcProfiles: Map<string, NPCProfile>
  enablePolygamy?: boolean
}

export function areSameFormId(a?: string | null, b?: string | null): boolean {
  if (!a || !b) return false
  const cleanA = a.replace(/^0x/i, '').toLowerCase().padStart(8, '0')
  const cleanB = b.replace(/^0x/i, '').toLowerCase().padStart(8, '0')
  return cleanA === cleanB
}

export function isPlayerFormId(id?: string | null): boolean {
  if (!id) return false
  return areSameFormId(id, PLAYER_FORM_ID) || areSameFormId(id, '00000014') || areSameFormId(id, '0x00000014')
}

export function getActorDisplayName(
  id: string,
  npcProfiles: Map<string, NPCProfile>
): string {
  if (isPlayerFormId(id)) return 'Player'
  const profile =
    npcProfiles.get(id) ||
    Array.from(npcProfiles.values()).find((p) => areSameFormId(p.formId, id))
  if (profile?.name && profile.name !== 'None') return profile.name
  return id
}

export function isMainTierStatus(status: RelationshipStatus): boolean {
  return (
    status === RelationshipStatus.Dating ||
    status === RelationshipStatus.Engaged ||
    status === RelationshipStatus.Married
  )
}

/**
 * Assignable statuses in the UI (Candidate is excluded per specification).
 */
export const ASSIGNABLE_STATUSES: RelationshipStatus[] = [
  RelationshipStatus.Dating,
  RelationshipStatus.Lover,
  RelationshipStatus.Engaged,
  RelationshipStatus.Married,
  RelationshipStatus.Divorced,
  RelationshipStatus.Jilted,
  RelationshipStatus.ExDating,
  RelationshipStatus.ExLover,
]

/**
 * Check availability and reasons for a specific target status between actorA and actorB.
 */
export function checkRelationshipStatusAvailability(
  actorA: string,
  actorB: string,
  targetStatus: RelationshipStatus,
  context: StatusAvailabilityContext
): { available: boolean; reason?: string; loverType?: 'allowed' | 'cheating' } {
  if (areSameFormId(actorA, actorB)) {
    return { available: false, reason: 'An actor cannot form a relationship with themselves.' }
  }

  const profileA =
    context.npcProfiles.get(actorA) ||
    Array.from(context.npcProfiles.values()).find((p) => areSameFormId(p.formId, actorA))
  const profileB =
    context.npcProfiles.get(actorB) ||
    Array.from(context.npcProfiles.values()).find((p) => areSameFormId(p.formId, actorB))

  if (profileA?.isChild || profileB?.isChild) {
    return { available: false, reason: 'Children cannot enter adult relationships.' }
  }

  const nameA = getActorDisplayName(actorA, context.npcProfiles)
  const nameB = getActorDisplayName(actorB, context.npcProfiles)

  // Find existing relationship pair between actorA and actorB
  const directPair = context.relationships.find(
    (r) =>
      (areSameFormId(r.actorA, actorA) && areSameFormId(r.actorB, actorB)) ||
      (areSameFormId(r.actorA, actorB) && areSameFormId(r.actorB, actorA))
  )
  const directStatus = directPair ? directPair.status : RelationshipStatus.Candidate

  if (targetStatus === directStatus) {
    return { available: false, reason: `Already ${getStatusLabel(directStatus)}.` }
  }

  // Determine Lover allowed vs cheating classification
  const evaluateLoverType = (): 'allowed' | 'cheating' => {
    const isActorInExclusiveMain = (actorId: string): boolean => {
      // Find all main relationships involving actorId (excluding the direct pair)
      const mainPairs = context.relationships.filter(
        (r) =>
          (areSameFormId(r.actorA, actorId) && !areSameFormId(r.actorB, actorB) && !areSameFormId(r.actorB, actorA)) ||
          (areSameFormId(r.actorB, actorId) && !areSameFormId(r.actorA, actorB) && !areSameFormId(r.actorA, actorA))
      ).filter((r) => isMainTierStatus(r.status))

      if (mainPairs.length === 0) return false
      // If any main pair is closed (!isOpen), actor is in an exclusive main relationship
      return mainPairs.some((r) => !r.isOpen)
    }

    const aExclusive = isActorInExclusiveMain(actorA)
    const bExclusive = isActorInExclusiveMain(actorB)

    // Allowed when:
    // - both don't have main partners
    // - both have main partners, and both are in open relationships
    // - either doesn't have main, and the other has main but open
    // In other words, neither actor is in a closed/exclusive main relationship.
    return aExclusive || bExclusive ? 'cheating' : 'allowed'
  }

  // 1. Lover rules
  if (targetStatus === RelationshipStatus.Lover) {
    if (directStatus === RelationshipStatus.Lover) {
      return { available: false, reason: `${nameA} and ${nameB} are already lovers.` }
    }
    if (isMainTierStatus(directStatus)) {
      return {
        available: false,
        reason: `${nameA} and ${nameB} already have an active relationship (${getStatusLabel(directStatus)}).`,
      }
    }
    const loverType = evaluateLoverType()
    return { available: true, loverType }
  }

  // 2. Breakup status rules
  if (targetStatus === RelationshipStatus.Divorced) {
    if (directStatus !== RelationshipStatus.Married) {
      return { available: false, reason: 'Can only divorce if currently Married.' }
    }
    return { available: true }
  }
  if (targetStatus === RelationshipStatus.Jilted) {
    if (directStatus !== RelationshipStatus.Engaged) {
      return { available: false, reason: 'Can only be jilted if currently Engaged.' }
    }
    return { available: true }
  }
  if (targetStatus === RelationshipStatus.ExDating) {
    if (directStatus !== RelationshipStatus.Dating) {
      return { available: false, reason: 'Can only transition to Ex-Dating if currently Dating.' }
    }
    return { available: true }
  }
  if (targetStatus === RelationshipStatus.ExLover) {
    if (directStatus !== RelationshipStatus.Lover) {
      return { available: false, reason: 'Can only transition to Ex-Lover if currently Lovers.' }
    }
    return { available: true }
  }

  // 3. Direct checks for Main Tier (Dating, Engaged, Married)
  if (targetStatus === RelationshipStatus.Dating && isMainTierStatus(directStatus)) {
    return {
      available: false,
      reason: `${nameA} and ${nameB} already have an active relationship (${getStatusLabel(directStatus)}).`,
    }
  }
  if (
    targetStatus === RelationshipStatus.Engaged &&
    (directStatus === RelationshipStatus.Engaged || directStatus === RelationshipStatus.Married)
  ) {
    return {
      available: false,
      reason: `${nameA} and ${nameB} are already engaged or married.`,
    }
  }
  if (targetStatus === RelationshipStatus.Married && directStatus === RelationshipStatus.Married) {
    return {
      available: false,
      reason: `${nameA} and ${nameB} are already married.`,
    }
  }

  // 4. Other partners check for Main Tier targets
  const validateActorWithOtherPartners = (
    selfId: string,
    otherId: string,
    name: string
  ): { available: boolean; reason?: string } => {
    const isPlayer = isPlayerFormId(selfId)
    if (isPlayer && context.enablePolygamy) {
      return { available: true }
    }

    for (const r of context.relationships) {
      const containsSelf = areSameFormId(r.actorA, selfId) || areSameFormId(r.actorB, selfId)
      const containsOther = areSameFormId(r.actorA, otherId) || areSameFormId(r.actorB, otherId)
      if (containsSelf && !containsOther) {
        const partnerId = areSameFormId(r.actorA, selfId) ? r.actorB : r.actorA
        const partnerName = getActorDisplayName(partnerId, context.npcProfiles)

        if (targetStatus === RelationshipStatus.Dating || targetStatus === RelationshipStatus.Married) {
          if (isMainTierStatus(r.status)) {
            return {
              available: false,
              reason: isPlayer
                ? 'Cannot enter another main relationship: Polygamy is disabled in settings.'
                : `${name} already has a committed main partner (${partnerName}).`,
            }
          }
        } else if (targetStatus === RelationshipStatus.Engaged) {
          if (r.status === RelationshipStatus.Engaged || r.status === RelationshipStatus.Married) {
            return {
              available: false,
              reason: isPlayer
                ? 'Cannot enter another engagement/marriage: Polygamy is disabled in settings.'
                : `${name} is already engaged or married to ${partnerName}.`,
            }
          }
          if (r.status === RelationshipStatus.Dating) {
            return {
              available: false,
              reason: isPlayer
                ? 'Cannot propose engagement to this actor while dating someone else.'
                : `${name} is dating ${partnerName} and can only get engaged to their dating partner.`,
            }
          }
        }
      }
    }
    return { available: true }
  }

  const checkA = validateActorWithOtherPartners(actorA, actorB, nameA)
  if (!checkA.available) return checkA

  const checkB = validateActorWithOtherPartners(actorB, actorA, nameB)
  if (!checkB.available) return checkB

  return { available: true }
}

/**
 * Returns the full list of assignable statuses with availability, labels, and tooltip reasons.
 */
export function getAvailableStatuses(
  actorA?: string | null,
  actorB?: string | null,
  context?: Partial<StatusAvailabilityContext>
): StatusOptionInfo[] {
  const store = useMarasStore.getState()
  const ctx: StatusAvailabilityContext = {
    relationships: context?.relationships ?? store.relationships,
    npcProfiles: context?.npcProfiles ?? store.npcProfiles,
    enablePolygamy: context?.enablePolygamy ?? store.settings.enablePolygamy,
  }

  if (!actorA || !actorB) {
    return ASSIGNABLE_STATUSES.map((st) => ({
      status: st,
      value: st,
      label: getStatusLabel(st),
      enabled: false,
      disabled: true,
      tooltip: 'Select both actors first.',
    }))
  }

  const directPair = ctx.relationships.find(
    (r) =>
      (areSameFormId(r.actorA, actorA) && areSameFormId(r.actorB, actorB)) ||
      (areSameFormId(r.actorA, actorB) && areSameFormId(r.actorB, actorA))
  )
  const currentStatus = directPair ? directPair.status : RelationshipStatus.Candidate

  const options: StatusOptionInfo[] = ASSIGNABLE_STATUSES.map((st) => {
    const res = checkRelationshipStatusAvailability(actorA, actorB, st, ctx)
    const isCurrent = st === currentStatus

    let label = getStatusLabel(st)
    if (st === RelationshipStatus.Lover && res.available) {
      label = res.loverType === 'cheating' ? 'Lover (Cheating)' : 'Lover (Allowed)'
    }

    let tooltip = res.reason
    if (res.available) {
      if (st === RelationshipStatus.Lover) {
        tooltip =
          res.loverType === 'cheating'
            ? 'Considered cheating: at least one party has a closed main relationship.'
            : 'Allowed secondary relationship: both parties are single or in open relationships.'
      }
    }

    return {
      status: st,
      value: st,
      label,
      enabled: res.available,
      disabled: !res.available,
      tooltip,
      isCurrent,
      loverType: res.loverType,
    }
  })

  // If the pair is currently a Candidate, include Candidate as disabled current status
  // so that the select trigger displays the current status correctly without allowing
  // setting Candidate on other pairs.
  if (currentStatus === RelationshipStatus.Candidate) {
    options.unshift({
      status: RelationshipStatus.Candidate,
      value: RelationshipStatus.Candidate,
      label: 'Candidate',
      enabled: false,
      disabled: true,
      tooltip: 'Current status (Candidate cannot be manually assigned).',
      isCurrent: true,
    })
  }

  return options
}

/**
 * React hook to get available statuses for actorA and actorB, automatically updating when store changes.
 */
export function useAvailableStatuses(
  actorA?: string | null,
  actorB?: string | null
): StatusOptionInfo[] {
  const relationships = useMarasStore((s) => s.relationships)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const enablePolygamy = useMarasStore((s) => s.settings.enablePolygamy)

  return useMemo(() => {
    return getAvailableStatuses(actorA, actorB, {
      relationships,
      npcProfiles,
      enablePolygamy,
    })
  }, [actorA, actorB, relationships, npcProfiles, enablePolygamy])
}
