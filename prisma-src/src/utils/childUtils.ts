import { ChildRecord } from '../types'
import { areSameFormId } from './statusAvailability'

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
 * Resolves child relationship status relative to a single actor or a pair of actors.
 */
export function getChildRelationshipStatus(
  child: Pick<ChildRecord, 'isAdopted' | 'bioMotherId' | 'bioFatherId' | 'isSecret' | 'isSecretForBioFather' | 'isSecretForPartner' | 'gender'>,
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

  // Pair evaluation (e.g., in Partner Panel)
  if (actorIdOrA && actorBId) {
    const isBioA = areSameFormId(bioMother, actorIdOrA) || areSameFormId(bioFather, actorIdOrA)
    const isBioB = areSameFormId(bioMother, actorBId) || areSameFormId(bioFather, actorBId)

    if (isBioA && isBioB) {
      return {
        statusLabel: `Shared Biological ${sonOrDaughter}`,
        badgeLabel: sonOrDaughter,
        badgeStyle: 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30',
      }
    }
    if (isBioA || isBioB) {
      const isSecretPartner = child.isSecretForPartner ?? child.isSecret
      if (isSecretPartner) {
        return {
          statusLabel: `${stepNoun} / ${sonOrDaughter} (Secret)`,
          badgeLabel: `${stepNoun} (Secret)`,
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
  if (isSecretPartner) {
    return {
      statusLabel: `${stepNoun} (Secret from Partner)`,
      badgeLabel: `${stepNoun} (Secret)`,
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
 * Resolves a parent's specific parental role relative to a child (e.g. Biological Mother, Step-father (secret), etc.)
 */
export function getParentRole(
  parentFormId: string,
  child: Pick<ChildRecord, 'isAdopted' | 'bioMotherId' | 'bioFatherId' | 'isSecret' | 'isSecretForBioFather' | 'isSecretForPartner'>,
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
  if (isSecretPartner) {
    return {
      roleLabel: isMale ? 'Step-father (secret)' : isFemale ? 'Step-mother (secret)' : 'Step-parent (secret)',
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
