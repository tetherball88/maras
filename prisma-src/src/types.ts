export enum RelationshipStatus {
  Candidate = 0,
  Engaged = 1,
  Married = 2,
  Divorced = 3,
  Jilted = 4,
  Dating = 5,
  Lover = 6,
  ExDating = 7,
  ExLover = 8,
  Unknown = 255,
}

export enum SocialClass {
  Outcast = 0,
  Poverty = 1,
  Working = 2,
  Middle = 3,
  Wealthy = 4,
  Religious = 5,
  Nobles = 6,
  Rulers = 7,
}

export enum SkillType {
  Warrior = 0,
  Mage = 1,
  Rogue = 2,
  Craftsman = 3,
  Ranger = 4,
  Orator = 5,
}

export enum Temperament {
  Proud = 0,
  Humble = 1,
  Jealous = 2,
  Romantic = 3,
  Independent = 4,
}

export enum AffairType {
  Casual = 0,
  Dating = 1,
  Lover = 2,
}

export enum AffairStatus {
  Ongoing = 0,
  EndedBreakup = 1,
  EndedVictimSplit = 2,
  Allowed = 3,
  EndedConfronted = 4,
}

export interface CheatingRecord {
  cheaterId: string
  victimId: string
  affairPartnerId: string
  count: number
  lastTimestamp: number
  isSecret: boolean
  affairType?: AffairType
  affairStatus?: AffairStatus
}

export type ChildStage = 'unborn' | 'infant' | 'child' | 'training' | 'adult'

export interface ChildRecord {
  childId: string
  name: string
  timestamp: number
  isAdopted?: boolean
  guardianAId?: string
  guardianBId?: string
  guardianAName?: string
  guardianBName?: string
  bioMotherId?: string
  bioFatherId?: string
  bioMotherName?: string
  bioFatherName?: string
  presumedFatherId?: string
  presumedFatherName?: string
  stage?: ChildStage
  isEphemeral?: boolean
  hasRefID?: boolean
  trainingLocation?: string
  isBiologicalChild?: boolean
  conceptionTimestamp?: number
  birthTimestamp?: number
  isSecret?: boolean
  isSecretForBioFather?: boolean
  isSecretForPartner?: boolean
  hasPhenotypeMismatch?: boolean
  discoveredByPartner?: boolean
  discoveredByBioFather?: boolean
  gender?: 'male' | 'female' | 'unknown' | 'undefined'
}

export interface StatusChangeRecord {
  fromStatus: RelationshipStatus
  toStatus: RelationshipStatus
  timestamp: number
  initiatorFormId: string
}

export interface PlayerHouse {
  formId: string
  name: string
  locationName?: string
}

export interface ChildCompanionInfo {
  formId: string
  name: string
}

export interface CompactNPC {
  formId: string
  name: string
  race: string
  gender?: 'male' | 'female' | 'unknown' | 'undefined'
  isChild?: boolean
  pluginName?: string
}

export interface NPCProfile {
  formId: string
  name: string
  race: string
  gender?: 'male' | 'female' | 'unknown' | 'undefined'
  socialClass: SocialClass
  skillType: SkillType
  temperament: Temperament
  currentHome?: string
  originalHome?: string
  homeMarker?: string
  stayAtHome?: boolean
  currentPackage?: string
  isChild?: boolean
  stage?: ChildStage
  isEphemeral?: boolean
  hasRefID?: boolean
  trainingLocation?: string
  critterBanned?: boolean
  petBanned?: boolean
  currentCritter?: ChildCompanionInfo | null
  currentPet?: ChildCompanionInfo | null
  pluginName?: string
  blockedFromAdoptionReason?: string
}

export interface RelationshipPair {
  actorA: string
  actorB: string
  status: RelationshipStatus
  isOpen: boolean
  statusHistory: StatusChangeRecord[]
}

export interface AffectionData {
  formId: string
  permanent: number
  decayMultiplier: number
  daysSinceLastInteraction: number
}

export interface MarasSettings {
  skipWedding: boolean
  affectionDecayMult: number
  enablePolygamy: boolean
  disablePolygamyQuest?: boolean
  toggleUIKey: number
  toggleUIModCtrl: boolean
  toggleUIModShift: boolean
  toggleUIModAlt: boolean
  startDialGender: number
  minRelRankForDial: number
  hasSkyrimNet: boolean
  simulateNoSkyrimNet?: boolean
  enableConfirmationModal?: boolean
  enableScenes?: boolean
  scenesCooldownHours?: number
  enableScenes_Lover?: boolean
  enableScenes_Dating?: boolean
  enableScenes_Engaged?: boolean
  enableScenes_Married?: boolean
  enableScenes_OpenRelationship?: boolean
  enableScenes_Cheating?: boolean
  enableScenes_CheatingDiscovered?: boolean
  enableScenes_CheatingCaught?: boolean
  // Location Types for Scenes
  // Interiors
  enableScenes_LocTypeTemple?: boolean
  enableScenes_LocTypeStore?: boolean
  enableScenes_LocTypeStewardsDwelling?: boolean
  enableScenes_LocTypeInn?: boolean
  enableScenes_LocTypeHouse?: boolean
  enableScenes_LocTypeBarracks?: boolean
  enableScenes_LocTypeGuild?: boolean
  // Player Houses
  enableScenes_LocTypePlayerHouse?: boolean
  enableScenes_BYOH_LocTypeHomestead?: boolean
  // Streets
  enableScenes_LocTypeTown?: boolean
  enableScenes_LocTypeSettlement?: boolean
  enableScenes_LocTypeOrcStronghold?: boolean
  enableScenes_LocTypeCity?: boolean
  enableScenes_LocTypeLumberMill?: boolean
  enableScenes_LocTypeFarm?: boolean
  enableConfirmPlayerNpc: boolean
  enableConfirmNpcNpc: boolean
  confirmStatusChange_Dating: boolean
  confirmStatusChange_Engagement: boolean
  confirmStatusChange_Lover: boolean
  confirmStatusChange_Marry: boolean
  confirmBreakup_Dating: boolean
  confirmBreakup_Engagement: boolean
  confirmBreakup_Lover: boolean
  confirmDivorce: boolean
  confirmDivorceDuringLowAffectionQuest: boolean
  confirmOpenRelationship: boolean
  confirmCloseRelationship: boolean
  alwaysSuccess: boolean
  debugSpell: boolean
  logLevel: number
  logDestination: number
  childCustodyGenderPolicy: number
  childCustodyPlayerPolicy: number
  childCustodyPrioritizeBioParent: boolean
  globalBanCritter?: boolean
  globalBanPet?: boolean
  skyrimnetActionRadius?: number
  matchmakingEnabled?: boolean
  matchmakingIntervalDays?: number
  matchmakingMaxSuggestions?: number
  matchmakingNpcCooldownDays?: number
  matchmakingAllowBreakups?: boolean
  matchmakingAllowOpenClose?: boolean
  matchmakingAllowCheating?: boolean
  matchmakingOrientation?: number
  matchmakingExcludePlayerActivePartners?: boolean
  matchmakingConfirmResults?: boolean
  paternityBaseChance?: number
  paternityDailyGrowth?: number
  paternityMaxChance?: number
  paternityPhenotypeBaseChance?: number
  paternityPhenotypeDailyGrowth?: number
  paternityPhenotypeMaxChance?: number
  paternityPhenotypeBirthChance?: number
  paternityNonSkyrimNetRevealChance?: number
  paternityBreakupChance?: number
  paternityBioStepUpChance?: number
  paternityForceDiceRolls?: boolean
  revealSecretsInUI?: boolean
  uiFullScreen?: boolean
  uiScale?: number
  autoPauseOnOpen?: boolean
}

export enum MatchmakingOrientation {
  Any = 0,
  Heterosexual = 1,
  Homosexual = 2,
  Gay = 3,
  Lesbian = 4,
  HeteroAndGay = 5,
  HeteroAndLesbian = 6,
}

export interface MatchmakingSettings {
  enabled: boolean
  intervalDays: number
  maxSuggestions: number
  npcCooldownDays: number
  allowBreakups: boolean
  allowOpenClose: boolean
  allowCheating: boolean
  orientation: MatchmakingOrientation
  excludePlayerActivePartners: boolean
  useLLM?: boolean
  generateMemories?: boolean
  evaluationStep?: boolean
  confirmResults?: boolean
  diceRollChanceNewDating?: number
  diceRollChanceNewEngaged?: number
  diceRollChanceNewLoverAllowed?: number
  diceRollChanceNewLoverCheating?: number
  diceRollChanceAdvance?: number
  diceRollChanceBreakup?: number
  diceRollChanceOpenClose?: number
}

export interface NpcMemoryEntry {
  actor?: string
  actorFormId?: string
  content: string
  location?: string
  emotion?: string
  importance_score?: number
  tags?: string[]
  type?: string
}

export interface AiMatchSuggestion {
  formId: string
  name?: string
  actorAFormId?: string
  actorAName?: string
  suggestedStatus: RelationshipStatus
  action?: string // "new", "advance", "breakup", "open", "close"
  targetStatus?: string
  selected: boolean
  compatibilityScore?: number
  reasoning?: string
  memory?: string
  dualMemories?: NpcMemoryEntry[]
  isGeneratingMemory?: boolean
  isCheating?: boolean
  synergyTags?: string[]
}

export interface DataDumpResult {
  success: boolean
  fileName?: string
  filePath?: string
  absolutePath?: string
  timestamp: string
  totalNPCs?: number
  error?: string
}

export interface BuffContributor {
  formId: string
  name: string
  rank?: number
  rankMultiplier?: number
  affection: number
  affectionMultiplier: number
  multiplier: number
  isFollower: boolean
}

export interface BuffItem {
  id: string
  name: string
  category: string
  categoryType: 'permanent' | 'follower'
  description: string
  baseValue: number
  unit: string
  type: string
  multiplier: number
  effectiveValue: number
  isActive: boolean
  contributors: BuffContributor[]
  potentialSpouses?: BuffContributor[]
}

export interface HouseholdBuffsData {
  permanentBuffs: BuffItem[]
  followerBuffs: BuffItem[]
  activePermanentCount: number
  activeFollowerCount: number
}

export * from './types/modal'

