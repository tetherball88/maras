import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { confirmRemoveChild } from '../../store/useModalStore'
import { RelationshipStatus, SocialClass, SkillType, Temperament, AffairStatus } from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge } from '../shared/StatusBadge'
import { NpcTraitBadges } from '../shared/TraitIcons'
import { ScrollArea } from '@/components/ui/scroll-area'
import { NpcDetailPartnerPanel } from './NpcDetailPartnerPanel'
import { MatchmakingModal } from './MatchmakingModal'
import { PlayerDetailBody } from './PlayerDetailBody'
import { NpcDetailBody } from './NpcDetailBody'
import { SpouseDetailBody } from './SpouseDetailBody'
import { ChildDetailBody } from './ChildDetailBody'
import { PlayerChildDetailBody } from './PlayerChildDetailBody'
import {
  ArrowLeft,
  Crown,
  HeartHandshake,
  Navigation,
  UserCheck,
  UserPlus,
  Sparkles,
  RefreshCw,
  Flame,
  AlertTriangle,
  Trash2,
} from 'lucide-react'
import { AppTooltip } from '../shared/AppTooltip'
import { areSameFormId } from '../../utils/statusAvailability'
import { getPlayerProfileFallback } from '../../utils/playerUtils'

export const NpcDetailShell: React.FC = () => {
  const npcId = useMarasStore((s) => s.npcDetailId)
  const closeNpcDetail = useMarasStore((s) => s.closeNpcDetail)

  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const cheatingRecords = useMarasStore((s) => s.cheatingRecords)
  const children = useMarasStore((s) => s.children)
  const settings = useMarasStore((s) => s.settings)

  const teleportPlayerToNpc = useMarasStore((s) => s.teleportPlayerToNpc)
  const teleportNpcToPlayer = useMarasStore((s) => s.teleportNpcToPlayer)
  const spawnChild = useMarasStore((s) => s.spawnChild)
  const removeChild = useMarasStore((s) => s.removeChild)
  const setMatchmakingModalOpen = useMarasStore((s) => s.setMatchmakingModalOpen)
  const evaluateChildSecret = useMarasStore((s) => s.evaluateChildSecret)
  const evaluatingChildId = useMarasStore((s) => s.evaluatingChildId)

  if (!npcId) return null

  const isPlayer = npcId === PLAYER_FORM_ID

  const npc = isPlayer
    ? getPlayerProfileFallback(npcProfiles)
    : npcProfiles.get(npcId)

  // Find relationship with player if applicable
  const relWithPlayer = !isPlayer
    ? relationships.find(
      (r) =>
        (areSameFormId(r.actorA, PLAYER_FORM_ID) && areSameFormId(r.actorB, npcId)) ||
        (areSameFormId(r.actorA, npcId) && areSameFormId(r.actorB, PLAYER_FORM_ID)),
    )
    : undefined

  // Check if this NPC is in an ongoing affair with the player
  const isAffairPartnerWithPlayer = !isPlayer && cheatingRecords.some(
    (cr) =>
      ((areSameFormId(cr.cheaterId, PLAYER_FORM_ID) && areSameFormId(cr.affairPartnerId, npcId)) ||
        (areSameFormId(cr.cheaterId, npcId) && areSameFormId(cr.affairPartnerId, PLAYER_FORM_ID))) &&
      (cr.affairStatus === undefined || cr.affairStatus === AffairStatus.Ongoing),
  )

  // Check if this NPC cheated on the player
  const hasCheatedOnPlayer = !isPlayer && cheatingRecords.some(
    (cr) =>
      areSameFormId(cr.cheaterId, npcId) &&
      areSameFormId(cr.victimId, PLAYER_FORM_ID) &&
      (cr.affairStatus === undefined || cr.affairStatus === AffairStatus.Ongoing),
  )

  const isRomanticPartnerToPlayer = !isPlayer && relWithPlayer && (
    relWithPlayer.status === RelationshipStatus.Married ||
    relWithPlayer.status === RelationshipStatus.Engaged ||
    relWithPlayer.status === RelationshipStatus.Dating ||
    relWithPlayer.status === RelationshipStatus.Lover
  )

  // Parent resolution for child NPCs
  const childRecord = children.get(npcId)
  const isChild = !!(npc?.isChild || childRecord)
  const stage = childRecord?.stage || npc?.stage || (isChild ? 'child' : undefined)
  const hasRefID = childRecord?.hasRefID ?? npc?.hasRefID ?? !npcId.startsWith('FF7')
  const isPlayerChild = isChild && !!(
    childRecord && (
      areSameFormId(childRecord.guardianAId, PLAYER_FORM_ID) ||
      areSameFormId(childRecord.guardianBId, PLAYER_FORM_ID) ||
      ((!childRecord.guardianAId || childRecord.guardianAId === '00000000') && (!childRecord.guardianBId || childRecord.guardianBId === '00000000') && (
        areSameFormId(childRecord.bioMotherId, PLAYER_FORM_ID) ||
        areSameFormId(childRecord.bioFatherId, PLAYER_FORM_ID)
      ))
    )
  )

  const hasSecret = Boolean(
    childRecord && (childRecord.isSecret || childRecord.isSecretForPartner || childRecord.isSecretForBioFather)
  )
  const isEvaluatingSecret = Boolean(
    childRecord && evaluatingChildId && areSameFormId(evaluatingChildId, childRecord.childId)
  )

  const isPlaceholderName = !npc?.name || npc.name.startsWith('NPC_')
  const resolvedChildName = childRecord?.name && !childRecord.name.startsWith('NPC_') ? childRecord.name : undefined
  const displayName = (isPlaceholderName && resolvedChildName) ? resolvedChildName : (npc?.name || childRecord?.name || `NPC ${npcId}`)
  const displayRace = npc?.race || 'Nord'

  // Render appropriate body component based on NPC resolution
  const renderBody = () => {
    if (isPlayer) {
      return <PlayerDetailBody />
    }
    if (isChild) {
      if (isPlayerChild) {
        return (
          <PlayerChildDetailBody
            npcId={npcId}
            displayName={displayName}
            displayRace={displayRace}
          />
        )
      }
      return (
        <ChildDetailBody
          npcId={npcId}
          displayName={displayName}
          displayRace={displayRace}
          isPlayerChild={false}
        />
      )
    }
    if (isRomanticPartnerToPlayer) {
      return <SpouseDetailBody npcId={npcId} />
    }
    return <NpcDetailBody npcId={npcId} />
  }

  return (
    <div className="flex-1 bg-panel border border-border-subtle rounded-lg flex flex-col h-full overflow-hidden relative">
      {/* Page Header */}
      <header className="p-[1rem] bg-panel-raised border-b border-border-divider flex items-center justify-between">
        <div className="flex items-center gap-[0.75rem]">
          <AppTooltip content="Back to Previous Page">
            <button
              onClick={closeNpcDetail}
              className="p-[0.375rem] rounded-md text-foreground-muted hover:text-accent-bright hover:bg-accent-soft cursor-pointer transition-colors"
            >
              <ArrowLeft className="w-[1.25em] h-[1.25em]" />
            </button>
          </AppTooltip>

          <div>
            <h2 className="text-xl font-bold text-accent-bright flex items-center gap-[0.5rem]">
              {isPlayer ? (
                <Crown className="w-[0.875em] h-[0.875em] text-accent" />
              ) : null}
              <span>{displayName}</span>
              <span className="text-xs font-mono font-normal text-foreground-muted bg-panel-inset px-[0.375rem] py-[0.125rem] rounded border border-border-subtle">
                {npcId}
              </span>
            </h2>
            <div className="text-xs text-foreground-secondary flex items-center gap-[0.5rem] mt-[0.125rem] flex-wrap">
              <span>{displayRace}</span>
              {npc?.pluginName && (
                <>
                  <span>•</span>
                  <span className="font-mono text-[0.6875rem] text-foreground-muted bg-panel-inset px-[0.375rem] py-[0.0625rem] rounded border border-border-subtle" title={`Plugin: ${npc.pluginName}`}>
                    {npc.pluginName}
                  </span>
                </>
              )}
              {isPlayer && (
                <>
                  <span>•</span>
                  <span
                    className={`px-[0.375rem] py-[0.125rem] rounded text-[0.6875rem] font-semibold border ${settings.enablePolygamy
                        ? 'bg-purple-500/20 text-purple-300 border-purple-500/40'
                        : 'bg-panel-inset text-foreground-muted border-border-subtle'
                      }`}
                  >
                    Polygamy: {settings.enablePolygamy ? 'Enabled' : 'Disabled'}
                  </span>
                </>
              )}
              {hasCheatedOnPlayer && (
                <>
                  <span>•</span>
                  <span className="text-[0.6875rem] px-[0.375rem] py-[0.125rem] rounded bg-rose-500/20 text-rose-400 border border-rose-500/30 font-semibold flex items-center gap-[0.25rem]">
                    <AlertTriangle className="w-[0.875em] h-[0.875em]" />
                    <span>Cheated</span>
                  </span>
                </>
              )}
              {isAffairPartnerWithPlayer && (
                <>
                  <span>•</span>
                  <span className="text-[0.6875rem] px-[0.375rem] py-[0.125rem] rounded bg-rose-500/20 text-rose-300 border border-rose-500/30 font-semibold flex items-center gap-[0.25rem]">
                    <Flame className="w-[0.875em] h-[0.875em]" />
                    <span>Affair Partner</span>
                  </span>
                </>
              )}
              {relWithPlayer &&
                relWithPlayer.status !== RelationshipStatus.Unknown &&
                relWithPlayer.status !== RelationshipStatus.Candidate && (
                  <>
                    <span>•</span>
                    <StatusBadge status={relWithPlayer.status} />
                  </>
                )}
              {isChild && (
                <>
                  <span>•</span>
                  {stage === 'unborn' ? (
                    <span className="px-[0.375rem] py-[0.125rem] bg-pink-500/20 text-pink-300 border border-pink-500/30 rounded text-[0.6875rem] font-bold">
                      🤰 Expecting / Unborn
                    </span>
                  ) : stage === 'infant' ? (
                    <span className="px-[0.375rem] py-[0.125rem] bg-cyan-500/20 text-cyan-300 border border-cyan-500/30 rounded text-[0.6875rem] font-bold">
                      🍼 Newborn Infant
                    </span>
                  ) : stage === 'training' ? (
                    <span className="px-[0.375rem] py-[0.125rem] bg-purple-500/20 text-purple-300 border border-purple-500/30 rounded text-[0.6875rem] font-bold">
                      🎓 In Training
                    </span>
                  ) : stage === 'adult' ? (
                    <span className="px-[0.375rem] py-[0.125rem] bg-amber-500/20 text-amber-300 border border-amber-500/30 rounded text-[0.6875rem] font-bold">
                      ⚔️ Adult Offspring
                    </span>
                  ) : !hasRefID ? (
                    <span className="px-[0.375rem] py-[0.125rem] bg-emerald-500/20 text-emerald-300 border border-emerald-500/30 rounded text-[0.6875rem] font-bold">
                      🧸 Child (Unspawned)
                    </span>
                  ) : (
                    <span className="px-[0.375rem] py-[0.125rem] bg-amber-500/20 text-amber-300 border border-amber-500/30 rounded text-[0.6875rem] font-bold">
                      Child
                    </span>
                  )}
                </>
              )}
              {npc && !isPlayer && !isChild && (
                <>
                  <span>•</span>
                  <NpcTraitBadges npc={npc} size="md" showIconOnly />
                </>
              )}
            </div>
          </div>
        </div>

        {/* Quick Action Buttons Bar */}
        <div className="flex items-center gap-[0.5rem]">
          {!isPlayer && hasRefID && (
            <>
              <AppTooltip content="Teleport Player to NPC location">
                <button
                  onClick={() => teleportPlayerToNpc(npcId)}
                  className="text-xs text-foreground-primary bg-panel-inset hover:bg-interactive-hover px-[0.625rem] py-[0.375rem] rounded border border-border-subtle transition-colors flex items-center gap-[0.25rem] cursor-pointer"
                >
                  <Navigation className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>Teleport Player To</span>
                </button>
              </AppTooltip>

              <AppTooltip content="Teleport NPC to Player location">
                <button
                  onClick={() => teleportNpcToPlayer(npcId)}
                  className="text-xs text-foreground-primary bg-panel-inset hover:bg-interactive-hover px-[0.625rem] py-[0.375rem] rounded border border-border-subtle transition-colors flex items-center gap-[0.25rem] cursor-pointer"
                >
                  <UserCheck className="w-[0.875em] h-[0.875em] text-accent" />
                  <span>Teleport NPC To Player</span>
                </button>
              </AppTooltip>
            </>
          )}

          {isChild && !hasRefID && stage === 'child' && (
            <AppTooltip content="Spawn physical child actor into the world (PlaceAtMe persistent)">
              <button
                onClick={() => spawnChild(npcId)}
                className="text-xs font-semibold text-emerald-200 bg-emerald-600/20 hover:bg-emerald-600/30 border border-emerald-500/40 px-[0.75rem] py-[0.375rem] rounded transition-colors flex items-center gap-[0.375rem] cursor-pointer shadow-sm"
              >
                <UserPlus className="w-[1em] h-[1em] text-emerald-400" />
                <span>Spawn Child</span>
              </button>
            </AppTooltip>
          )}

          {settings.hasSkyrimNet && isChild && hasSecret && (
            <AppTooltip
              content={
                isEvaluatingSecret
                  ? 'Evaluating paternity secret via SkyrimNet LLM...'
                  : 'Evaluate secret via SkyrimNet AI'
              }
            >
              <button
                onClick={() => childRecord && evaluateChildSecret(childRecord.childId)}
                disabled={isEvaluatingSecret}
                className={`text-xs font-semibold px-[0.75rem] py-[0.375rem] rounded border transition-colors flex items-center gap-[0.375rem] ${isEvaluatingSecret
                  ? 'opacity-50 cursor-not-allowed bg-purple-950/20 text-purple-400/60 border-purple-900/30'
                  : 'text-purple-200 bg-purple-500/20 hover:bg-purple-500/30 border-purple-500/40 cursor-pointer shadow-sm shadow-purple-950/40'
                  }`}
              >
                {isEvaluatingSecret ? (
                  <RefreshCw className="w-[1em] h-[1em] text-purple-300 animate-spin" />
                ) : (
                  <Sparkles className="w-[1em] h-[1em] text-purple-300" />
                )}
                <span>{isEvaluatingSecret ? 'Evaluating...' : 'Evaluate Secret'}</span>
              </button>
            </AppTooltip>
          )}

          {childRecord && (
            <AppTooltip content="Permanently remove this child record from M.A.R.A.S household and family tracking">
              <button
                onClick={() =>
                  confirmRemoveChild(npcId, displayName, () => {
                    removeChild(npcId)
                    closeNpcDetail()
                  })
                }
                className="text-xs font-medium bg-rose-950/40 text-status-danger hover:bg-rose-900/50 border border-rose-800/50 hover:border-rose-700/70 px-[0.625rem] py-[0.375rem] rounded transition-colors flex items-center gap-[0.375rem] cursor-pointer"
              >
                <Trash2 className="w-[0.95em] h-[0.95em]" />
                <span>Remove Child Record</span>
              </button>
            </AppTooltip>
          )}

          {!isChild && (
            <AppTooltip content="Open Matchmaking Modal">
              <button
                onClick={() => setMatchmakingModalOpen(true, npcId)}
                className="text-xs font-semibold text-accent-bright bg-accent-soft hover:bg-accent/20 px-[0.75rem] py-[0.375rem] rounded border border-border-accent transition-colors flex items-center gap-[0.375rem] cursor-pointer"
              >
                <HeartHandshake className="w-[1em] h-[1em] text-accent" />
                <span>Matchmaking</span>
              </button>
            </AppTooltip>
          )}
        </div>
      </header>

      {/* Main Content Body */}
      <div className="flex-1 flex min-h-0 overflow-hidden relative">
        <ScrollArea className="flex-1 p-[1.25rem]">
          {renderBody()}
        </ScrollArea>

        {/* Nested Right Drawer for Partner Detail */}
        <NpcDetailPartnerPanel />
      </div>

      {/* Matchmaking Modal */}
      <MatchmakingModal />
    </div>
  )
}
