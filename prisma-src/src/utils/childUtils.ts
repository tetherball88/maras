import { AffairStatus, ChildRecord, CheatingRecord, RelationshipPair, RelationshipStatus } from '../types'
import { areSameFormId } from './statusAvailability'
import { PLAYER_FORM_ID } from '../data/mockData'

export interface ChildStatusInfo {
  statusLabel: string
  badgeLabel: string
  badgeStyle: string
}

export interface ParentRoleInfo {
  roleLabel: string
  isSecret?: boolean
  isSecretForPartner?: boolean
  isSecretForBioFather?: boolean
  badgeStyle?: string
}

export function resolveIsFemale(
  child?: { gender?: string; [key: string]: any } | null,
  npc?: { gender?: string; [key: string]: any } | null
): boolean | undefined {
  const g = child?.gender ?? npc?.gender
  if (g === 'female') return true
  if (g === 'male') return false
  return undefined
}

export function getChildGenderNouns(isFemale?: boolean) {
  const sonOrDaughter = isFemale !== undefined ? (isFemale ? 'Daughter' : 'Son') : 'Child'
  const childNoun = isFemale !== undefined ? (isFemale ? 'Daughter' : 'Son') : 'Child'
  const stepNoun = isFemale !== undefined ? (isFemale ? 'Stepdaughter' : 'Stepson') : 'Stepchild'
  const boyOrGirl = isFemale !== undefined ? (isFemale ? 'Girl' : 'Boy') : 'Child'
  const genderSymbol = isFemale !== undefined ? (isFemale ? '♀' : '♂') : ''
  return { sonOrDaughter, childNoun, stepNoun, boyOrGirl, genderSymbol }
}

/**
 * Checks whether an actor is associated with a child as guardian, biological parent, or active presumed father.
 */
export function isActorChild(
  child: Pick<
    ChildRecord,
    | 'guardianAId'
    | 'guardianBId'
    | 'bioMotherId'
    | 'bioFatherId'
    | 'presumedFatherId'
    | 'isSecretForPartner'
    | 'isSecret'
  >,
  actorId?: string | null,
): boolean {
  if (!actorId) return false
  if (
    areSameFormId(child.guardianAId, actorId) ||
    areSameFormId(child.guardianBId, actorId) ||
    areSameFormId(child.bioMotherId, actorId) ||
    areSameFormId(child.bioFatherId, actorId)
  ) {
    return true
  }
  const isSecretPartner = Boolean(child.isSecretForPartner ?? child.isSecret)
  if (isSecretPartner && child.presumedFatherId && areSameFormId(child.presumedFatherId, actorId)) {
    return true
  }
  return false
}

/**
 * Resolves child relationship status relative to a single actor or a pair of actors.
 */
export function getChildRelationshipStatus(
  child: Pick<
    ChildRecord,
    | 'isAdopted'
    | 'guardianAId'
    | 'guardianBId'
    | 'bioMotherId'
    | 'bioFatherId'
    | 'presumedFatherId'
    | 'isSecret'
    | 'isSecretForBioFather'
    | 'isSecretForPartner'
    | 'gender'
  >,
  actorIdOrA?: string | null,
  actorBId?: string | null,
): ChildStatusInfo {
  const isAdopted = child.isAdopted !== false
  const isFemale = resolveIsFemale(child)
  const { sonOrDaughter, childNoun, stepNoun } = getChildGenderNouns(isFemale)

  if (isAdopted) {
    return {
      statusLabel: `Adopted ${childNoun}`,
      badgeLabel: `Adopted ${sonOrDaughter}`,
      badgeStyle: 'bg-amber-500/20 text-amber-300 border border-amber-500/30',
    }
  }

  const bioMother = child.bioMotherId
  const bioFather = child.bioFatherId
  const presumedFather = child.presumedFatherId

  // Pair evaluation (e.g., in Partner Panel)
  if (actorIdOrA && actorBId) {
    const isBioA = areSameFormId(bioMother, actorIdOrA) || areSameFormId(bioFather, actorIdOrA)
    const isBioB = areSameFormId(bioMother, actorBId) || areSameFormId(bioFather, actorBId)
    const isPresumedA = Boolean(presumedFather && areSameFormId(presumedFather, actorIdOrA))
    const isPresumedB = Boolean(presumedFather && areSameFormId(presumedFather, actorBId))

    if (isBioA && isBioB) {
      return {
        statusLabel: `Shared Biological ${sonOrDaughter}`,
        badgeLabel: sonOrDaughter,
        badgeStyle: 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30',
      }
    }
    if ((isBioA && isPresumedB) || (isBioB && isPresumedA)) {
      const isSecretPartner = child.isSecretForPartner ?? child.isSecret
      if (isSecretPartner) {
        return {
          statusLabel: `Presumed ${sonOrDaughter} (Secret)`,
          badgeLabel: `Presumed ${sonOrDaughter}`,
          badgeStyle: 'bg-purple-500/20 text-purple-300 border border-purple-500/30',
        }
      }
      return {
        statusLabel: `${stepNoun} / ${sonOrDaughter}`,
        badgeLabel: stepNoun,
        badgeStyle: 'bg-sky-500/20 text-sky-300 border border-sky-500/30',
      }
    }
    if (isBioA || isBioB) {
      const isSecretPartner = child.isSecretForPartner ?? child.isSecret
      if (isSecretPartner) {
        return {
          statusLabel: `Presumed ${sonOrDaughter} (Secret)`,
          badgeLabel: `Presumed ${sonOrDaughter}`,
          badgeStyle: 'bg-purple-500/20 text-purple-300 border border-purple-500/30',
        }
      }
      return {
        statusLabel: `${stepNoun} / ${sonOrDaughter}`,
        badgeLabel: stepNoun,
        badgeStyle: 'bg-sky-500/20 text-sky-300 border border-sky-500/30',
      }
    }
    return {
      statusLabel: `Adopted ${childNoun}`,
      badgeLabel: `Adopted ${sonOrDaughter}`,
      badgeStyle: 'bg-amber-500/20 text-amber-300 border border-amber-500/30',
    }
  }

  // Single actor evaluation (Player or NPC detail view)
  const isBioMotherMatch = actorIdOrA ? areSameFormId(bioMother, actorIdOrA) : false
  const isBioFatherMatch = actorIdOrA ? areSameFormId(bioFather, actorIdOrA) : false
  const isPresumedFatherMatch = actorIdOrA && presumedFather ? areSameFormId(presumedFather, actorIdOrA) : false
  const isGuardianMatch = actorIdOrA
    ? areSameFormId(child.guardianAId, actorIdOrA) || areSameFormId(child.guardianBId, actorIdOrA)
    : false

  if (isBioMotherMatch || isBioFatherMatch) {
    if (isBioFatherMatch && child.isSecretForBioFather) {
      return {
        statusLabel: `Biological ${sonOrDaughter} (Secret from Father)`,
        badgeLabel: `${sonOrDaughter} (Secret)`,
        badgeStyle: 'bg-indigo-500/20 text-indigo-300 border border-indigo-500/30',
      }
    }
    return {
      statusLabel: `Biological ${sonOrDaughter}`,
      badgeLabel: sonOrDaughter,
      badgeStyle: 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30',
    }
  }

  const isSecretPartner = child.isSecretForPartner ?? child.isSecret
  if (isPresumedFatherMatch) {
    if (isSecretPartner) {
      return {
        statusLabel: isGuardianMatch
          ? `Presumed ${sonOrDaughter} (Guardian • Secret)`
          : `Presumed ${sonOrDaughter} (Secret from Partner)`,
        badgeLabel: `Presumed ${sonOrDaughter}`,
        badgeStyle: 'bg-purple-500/20 text-purple-300 border border-purple-500/30',
      }
    }
    if (isGuardianMatch) {
      return {
        statusLabel: stepNoun,
        badgeLabel: stepNoun,
        badgeStyle: 'bg-sky-500/20 text-sky-300 border border-sky-500/30',
      }
    }
    return {
      statusLabel: `Former Presumed ${sonOrDaughter}`,
      badgeLabel: `Former Presumed`,
      badgeStyle: 'bg-sky-500/20 text-sky-300 border border-sky-500/30',
    }
  }

  if (isSecretPartner) {
    return {
      statusLabel: `Presumed ${sonOrDaughter} (Secret from Partner)`,
      badgeLabel: `Presumed ${sonOrDaughter}`,
      badgeStyle: 'bg-purple-500/20 text-purple-300 border border-purple-500/30',
    }
  }

  return {
    statusLabel: stepNoun,
    badgeLabel: stepNoun,
    badgeStyle: 'bg-sky-500/20 text-sky-300 border border-sky-500/30',
  }
}

/**
 * Resolves a parent's specific parental role relative to a child (e.g. Biological Mother, Presumed Father, Step-father, etc.)
 */
export function getParentRole(
  parentFormId: string,
  child: Pick<
    ChildRecord,
    'isAdopted' | 'bioMotherId' | 'bioFatherId' | 'presumedFatherId' | 'isSecret' | 'isSecretForBioFather' | 'isSecretForPartner'
  >,
  parentGender?: 'male' | 'female' | string,
): ParentRoleInfo {
  const isFemale = parentGender === 'female'
  const isMale = parentGender === 'male'
  if (child.isAdopted !== false) {
    return {
      roleLabel: isMale ? 'Adoptive Father' : isFemale ? 'Adoptive Mother' : 'Current Guardian',
      badgeStyle: 'text-amber-300',
    }
  }

  const isBioMother = areSameFormId(parentFormId, child.bioMotherId)
  const isBioFather = areSameFormId(parentFormId, child.bioFatherId)
  const isPresumedFather = Boolean(child.presumedFatherId && areSameFormId(parentFormId, child.presumedFatherId))

  if (isBioMother) {
    return {
      roleLabel: 'Biological Mother',
      isSecret: false,
      badgeStyle: 'text-emerald-300',
    }
  }

  if (isBioFather) {
    const isSecretBio = child.isSecretForBioFather ?? false
    return {
      roleLabel: 'Biological Father',
      isSecret: isSecretBio,
      isSecretForBioFather: isSecretBio,
      badgeStyle: 'text-emerald-300',
    }
  }

  // Not a biological parent, but part of the raising household / partner
  const isSecretPartner = child.isSecretForPartner ?? child.isSecret
  if (isPresumedFather) {
    if (isSecretPartner) {
      return {
        roleLabel: 'Presumed Father (Guardian)',
        isSecret: true,
        isSecretForPartner: true,
        badgeStyle: 'text-purple-300',
      }
    }
    return {
      roleLabel: 'Step-father',
      badgeStyle: 'text-sky-300',
    }
  }

  if (isSecretPartner) {
    return {
      roleLabel: isMale ? 'Presumed Father (Guardian)' : isFemale ? 'Step-mother (secret)' : 'Step-parent (secret)',
      isSecret: true,
      isSecretForPartner: true,
      badgeStyle: 'text-purple-300',
    }
  }

  return {
    roleLabel: isMale ? 'Step-father' : isFemale ? 'Step-mother' : 'Step-parent',
    badgeStyle: 'text-sky-300',
  }
}

/**
 * Masks an individual ChildRecord when revealSecrets is false so that undiscovered
 * paternity secrets appear as normal non-secret parentage (presumed father shown as biological father).
 */
export function maskChildRecord(
  child: ChildRecord,
  revealSecrets: boolean,
  npcProfiles?: Map<string, { name?: string }>,
): ChildRecord {
  if (revealSecrets) return child

  const isSecretPartner = Boolean(child.isSecretForPartner ?? child.isSecret)
  const isSecretBio = Boolean(child.isSecretForBioFather)
  if (!isSecretPartner && !isSecretBio) {
    return child
  }

  const cleanId = (id?: string) => (id && id !== '00000000' && id !== '00700000' ? id : undefined)

  if (isSecretPartner) {
    const presumedId = cleanId(child.presumedFatherId)
    const presumedName =
      child.presumedFatherName || (presumedId ? npcProfiles?.get(presumedId)?.name : undefined)
    return {
      ...child,
      ...(child.isAdopted === false
        ? {
            bioFatherId: presumedId,
            bioFatherName: presumedName,
          }
        : {}),
      presumedFatherId: undefined,
      presumedFatherName: undefined,
      isSecret: false,
      isSecretForPartner: false,
      isSecretForBioFather: false,
      hasPhenotypeMismatch: false,
    }
  }

  // Secret from biological father only (no partner secret)
  return {
    ...child,
    bioFatherId: undefined,
    bioFatherName: undefined,
    isSecret: false,
    isSecretForPartner: false,
    isSecretForBioFather: false,
    hasPhenotypeMismatch: false,
  }
}

export function maskChildrenMap(
  rawChildren: Map<string, ChildRecord>,
  revealSecrets: boolean,
  npcProfiles?: Map<string, { name?: string }>,
): Map<string, ChildRecord> {
  if (revealSecrets) return new Map(rawChildren)
  const masked = new Map<string, ChildRecord>()
  for (const [id, child] of rawChildren.entries()) {
    masked.set(id, maskChildRecord(child, false, npcProfiles))
  }
  return masked
}

/**
 * Masks undiscovered cheating records and secret affair relationship pairs when revealSecrets is false.
 */
export function maskCheatingAndRelationships(
  rawRelationships: RelationshipPair[],
  rawCheatingRecords: CheatingRecord[],
  revealSecrets: boolean,
): { relationships: RelationshipPair[]; cheatingRecords: CheatingRecord[] } {
  if (revealSecrets) {
    return {
      relationships: [...rawRelationships],
      cheatingRecords: [...rawCheatingRecords],
    }
  }

  const visibleCheating = rawCheatingRecords.filter((cr) => !cr.isSecret || cr.affairStatus === AffairStatus.Allowed)

  const isSecretAffairPair = (actorA: string, actorB: string): boolean => {
    let hasSecret = false
    let hasPublic = false
    for (const cr of rawCheatingRecords) {
      const matchesPair =
        (areSameFormId(cr.cheaterId, actorA) && areSameFormId(cr.affairPartnerId, actorB)) ||
        (areSameFormId(cr.cheaterId, actorB) && areSameFormId(cr.affairPartnerId, actorA))
      if (matchesPair) {
        if (cr.isSecret && cr.affairStatus !== AffairStatus.Allowed) hasSecret = true
        else hasPublic = true
      }
    }
    return hasSecret && !hasPublic
  }

  const visibleRelationships: RelationshipPair[] = []
  for (const r of rawRelationships) {
    const isAffairStatus =
      r.status === RelationshipStatus.Lover ||
      r.status === RelationshipStatus.Dating ||
      r.status === RelationshipStatus.ExLover ||
      r.status === RelationshipStatus.ExDating ||
      r.status === RelationshipStatus.Candidate

    if (isAffairStatus && isSecretAffairPair(r.actorA, r.actorB)) {
      let priorStatus: RelationshipStatus | null = null
      if (r.statusHistory && r.statusHistory.length > 0) {
        for (let i = r.statusHistory.length - 1; i >= 0; i--) {
          const histStatus = r.statusHistory[i].toStatus
          if (histStatus === RelationshipStatus.Divorced || histStatus === RelationshipStatus.Jilted) {
            priorStatus = histStatus
            break
          }
        }
      }

      if (priorStatus !== null) {
        visibleRelationships.push({
          ...r,
          status: priorStatus,
          isOpen: false,
          statusHistory: r.statusHistory.filter(
            (h) => h.toStatus !== RelationshipStatus.Lover && h.toStatus !== RelationshipStatus.Dating,
          ),
        })
      } else if (areSameFormId(r.actorA, PLAYER_FORM_ID) || areSameFormId(r.actorB, PLAYER_FORM_ID)) {
        visibleRelationships.push({
          ...r,
          status: RelationshipStatus.Candidate,
          isOpen: false,
          statusHistory: [],
        })
      }
      // For NPC-NPC secret affairs with no prior Divorced/Jilted history, omit the pair while secret
      continue
    }
    visibleRelationships.push(r)
  }

  return {
    relationships: visibleRelationships,
    cheatingRecords: visibleCheating,
  }
}
