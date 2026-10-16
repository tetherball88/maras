import { PLAYER_FORM_ID } from '../data/mockData'
import { NPCProfile, SocialClass, SkillType, Temperament } from '../types'

/**
 * Returns the formatted display name for an actor, dynamically retrieving
 * the player's custom character name if the formId matches the player character.
 */
export function formatActorName(
  formId: string,
  npcProfiles?: Map<string, NPCProfile> | null,
  fallback?: string,
): string {
  if (formId === PLAYER_FORM_ID) {
    return npcProfiles?.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
  }
  return npcProfiles?.get(formId)?.name || fallback || `NPC (${formId.slice(-6)})`
}

/**
 * Returns a fallback/default profile object for the player character,
 * populated with the dynamic player name if available in npcProfiles.
 */
export function getPlayerProfileFallback(npcProfiles?: Map<string, NPCProfile> | null): NPCProfile {
  const existing = npcProfiles?.get(PLAYER_FORM_ID)
  if (existing) return existing

  return {
    formId: PLAYER_FORM_ID,
    name: 'Prisoner (Player)',
    race: 'Nord',
    gender: 'female',
    pluginName: 'Skyrim.esm',
    socialClass: SocialClass.Rulers,
    skillType: SkillType.Warrior,
    temperament: Temperament.Proud,
    currentHome: 'Breezehome',
    originalHome: 'Breezehome',
  }
}
