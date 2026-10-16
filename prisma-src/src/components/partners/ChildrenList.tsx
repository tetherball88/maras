import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Baby, ChevronRight, Home, HeartHandshake, Sparkles, GraduationCap, Swords } from 'lucide-react'
import { AppTooltip } from '../shared/AppTooltip'
import { formatGameDay } from '../../utils/dateUtils'
import { getChildRelationshipStatus, resolveIsFemale, getChildGenderNouns } from '../../utils/childUtils'
import { ChildStage } from '../../types'

export const ChildrenList: React.FC = () => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const childrenStore = useMarasStore((s) => s.children)
  const npcDetailId = useMarasStore((s) => s.npcDetailId)
  const setActiveTab = useMarasStore((s) => s.setActiveTab)
  const setActiveSubSection = useMarasStore((s) => s.setActiveSubSection)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)

  const children = Array.from(childrenStore.values())
    .filter(
      (c) =>
        c.guardianAId === PLAYER_FORM_ID ||
        c.guardianBId === PLAYER_FORM_ID ||
        c.bioMotherId === PLAYER_FORM_ID ||
        c.bioFatherId === PLAYER_FORM_ID,
    )
    .map((c) => {
      const partnerId = c.guardianAId === PLAYER_FORM_ID ? c.guardianBId : c.guardianAId
      const partnerNpc = partnerId ? npcProfiles.get(partnerId) : undefined
      const partnerName = partnerNpc?.name || (partnerId ? 'Player' : '')
      return {
        ...c,
        isAdopted: c.isAdopted ?? true,
        parentPartnerName: partnerName,
      }
    })

  if (children.length === 0) {
    return (
      <div className="flex-1 flex flex-col items-center justify-center p-[2rem] text-center bg-panel rounded-lg border border-border-subtle">
        <div className="p-[1rem] bg-accent-soft rounded-full mb-[0.75rem] text-accent">
          <Baby className="w-[2rem] h-[2rem]" />
        </div>
        <h3 className="text-base font-bold text-foreground-primary">No Children in Household</h3>
        <p className="text-xs text-foreground-secondary max-w-[22rem] mt-[0.25rem] mb-[1.25rem]">
          Your household does not have any adopted or natural children registered yet.
        </p>
        <button
          onClick={() => {
            setActiveTab('matchmaking')
            setActiveSubSection('adoption')
          }}
          className="flex items-center gap-[0.5rem] px-[0.875rem] py-[0.5rem] bg-accent text-white hover:bg-accent/90 rounded-md text-xs font-semibold shadow transition-colors cursor-pointer"
        >
          <HeartHandshake className="w-[1.125em] h-[1.125em]" />
          <span>Open Child Adoption</span>
        </button>
      </div>
    )
  }

  return (
    <ScrollArea className="flex-1 min-h-0 pr-[0.5rem] h-full">
      <div className="space-y-[0.75rem]">
        <div className="flex items-center justify-between p-[0.5rem] px-[0.75rem] bg-panel-inset rounded border border-border-divider">
          <div className="flex items-center gap-[0.5rem] text-xs font-bold text-accent-bright">
            <Baby className="w-[1.125em] h-[1.125em] text-accent" />
            <span>Household Children</span>
            <span className="text-foreground-muted font-normal">({children.length})</span>
          </div>
          <button
            onClick={() => {
              setActiveTab('matchmaking')
              setActiveSubSection('adoption')
            }}
            className="text-[0.6875rem] font-semibold text-accent hover:underline flex items-center gap-[0.25rem] cursor-pointer"
          >
            <Sparkles className="w-[0.875em] h-[0.875em]" />
            <span>Adopt More</span>
          </button>
        </div>

        <div className="space-y-[0.375rem]">
          {children.map((child) => {
            const npc = npcProfiles.get(child.childId)
            const isSelected = npcDetailId === child.childId
            const isPlaceholderName = !npc?.name || npc.name.startsWith('NPC_')
            const displayName = (isPlaceholderName && child.name && !child.name.startsWith('NPC_'))
              ? child.name
              : (npc?.name || child.name || `Child ${child.childId}`)
            const race = npc?.race || 'Nord'
            const home = npc?.currentHome || npc?.originalHome || 'Breezehome'

            const stage: ChildStage = child.stage || npc?.stage || 'child'
            const hasRefID = child.hasRefID ?? npc?.hasRefID ?? !child.childId.startsWith('FF7')
            const isEphemeral = child.isEphemeral ?? npc?.isEphemeral ?? (!hasRefID || stage === 'unborn' || stage === 'infant' || stage === 'training')
            const isFemale = resolveIsFemale(child, npc)
            const { sonOrDaughter } = getChildGenderNouns(isFemale)

            return (
              <div
                key={child.childId}
                onClick={() => openNpcDetail(child.childId)}
                className={`flex items-center justify-between p-[0.625rem] rounded-md transition-all cursor-pointer ${isSelected
                  ? 'bg-accent-soft border-l-2 border-l-accent border-t border-r border-b border-border-accent'
                  : 'bg-panel-raised border border-border-subtle hover:border-border-strong hover:bg-interactive-hover'
                  }`}
              >
                <div className="flex-1 min-w-0 pr-[0.5rem]">
                  <div className="flex items-center gap-[0.375rem] flex-wrap">
                    <span className="text-sm font-semibold text-foreground-primary truncate">
                      {displayName}
                    </span>
                    <span className="text-xs font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle">
                      {child.childId}
                    </span>
                    <span className="text-xs text-foreground-muted">({race})</span>
                    {isFemale !== undefined && (
                      <span className={`text-[0.625rem] font-bold px-[0.25rem] py-[0.0625rem] rounded border ${isFemale ? 'bg-pink-500/20 text-pink-300 border-pink-500/30' : 'bg-blue-500/20 text-blue-300 border-blue-500/30'}`} title={isFemale ? 'Daughter' : 'Son'}>
                        {isFemale ? '♀ Daughter' : '♂ Son'}
                      </span>
                    )}
                    {npc?.pluginName && (
                      <span className="text-[0.625rem] font-mono text-foreground-muted bg-panel-inset px-[0.25rem] py-[0.0625rem] rounded border border-border-subtle truncate max-w-[8rem]" title={`Plugin: ${npc.pluginName}`}>
                        {npc.pluginName}
                      </span>
                    )}

                    {/* Stage & Lifecycle Badges */}
                    {stage === 'unborn' ? (
                      <span className="text-[0.625rem] font-bold px-[0.375rem] py-[0.0625rem] rounded border bg-pink-500/20 text-pink-300 border-pink-500/30 flex items-center gap-[0.25rem]">
                        <span>🤰 Expecting / Unborn {sonOrDaughter}</span>
                      </span>
                    ) : stage === 'infant' ? (
                      <span className="text-[0.625rem] font-bold px-[0.375rem] py-[0.0625rem] rounded border bg-cyan-500/20 text-cyan-300 border-cyan-500/30 flex items-center gap-[0.25rem]">
                        <Baby className="w-[0.875em] h-[0.875em]" />
                        <span>Newborn Infant {sonOrDaughter}</span>
                      </span>
                    ) : stage === 'training' ? (
                      <span className="text-[0.625rem] font-bold px-[0.375rem] py-[0.0625rem] rounded border bg-purple-500/20 text-purple-300 border-purple-500/30 flex items-center gap-[0.25rem]">
                        <GraduationCap className="w-[0.875em] h-[0.875em]" />
                        <span>In Training ({sonOrDaughter})</span>
                      </span>
                    ) : stage === 'adult' ? (
                      <span className="text-[0.625rem] font-bold px-[0.375rem] py-[0.0625rem] rounded border bg-amber-500/20 text-amber-300 border-amber-500/30 flex items-center gap-[0.25rem]">
                        <Swords className="w-[0.875em] h-[0.875em]" />
                        <span>{hasRefID ? `Adult Follower (${sonOrDaughter})` : `Adult ${sonOrDaughter} (Unsummoned)`}</span>
                      </span>
                    ) : !hasRefID ? (
                      <span className="text-[0.625rem] font-bold px-[0.375rem] py-[0.0625rem] rounded border bg-emerald-500/20 text-emerald-300 border-emerald-500/30">
                        🧸 {sonOrDaughter} (Unspawned)
                      </span>
                    ) : (
                      (() => {
                        const { badgeLabel, badgeStyle } = getChildRelationshipStatus(child, PLAYER_FORM_ID)
                        return (
                          <span
                            className={`text-[0.625rem] font-bold px-[0.375rem] py-[0.0625rem] rounded border ${badgeStyle}`}
                          >
                            {badgeLabel}
                          </span>
                        )
                      })()
                    )}
                  </div>

                  <div className="text-xs text-foreground-secondary truncate mt-[0.25rem] flex items-center gap-[0.5rem] flex-wrap">
                    {stage === 'unborn' ? (
                      <span className="flex items-center gap-[0.25rem] text-pink-200">
                        <span>🤰 In Gestation (Maternal Care)</span>
                      </span>
                    ) : stage === 'infant' ? (
                      <span className="flex items-center gap-[0.25rem] text-cyan-200">
                        <Baby className="w-[0.875em] h-[0.875em] text-cyan-400" />
                        <span>In Arms / Swaddled (Mother's Care)</span>
                      </span>
                    ) : stage === 'training' ? (
                      <span className="flex items-center gap-[0.25rem] text-purple-200">
                        <GraduationCap className="w-[0.875em] h-[0.875em] text-purple-400" />
                        <span>Training at {child.trainingLocation || 'Skyrim Academy'}</span>
                      </span>
                    ) : stage === 'adult' && !hasRefID ? (
                      <span className="flex items-center gap-[0.25rem] text-amber-200">
                        <Swords className="w-[0.875em] h-[0.875em] text-amber-400" />
                        <span>Ready to Summon via Follower Dialogue / MCM</span>
                      </span>
                    ) : (
                      <span className="flex items-center gap-[0.25rem]">
                        <Home className="w-[0.875em] h-[0.875em] text-foreground-muted" />
                        <span>{home}</span>
                      </span>
                    )}

                    <span>•</span>
                    <span>{child.parentPartnerName ? `Parents: Player & ${child.parentPartnerName}` : 'Guardian: Player (Sole Guardian)'}</span>
                    {stage === 'unborn' ? (
                      child.conceptionTimestamp != null && child.conceptionTimestamp > 0 && (
                        <>
                          <span>•</span>
                          <span>Conceived {formatGameDay(child.conceptionTimestamp, currentGameDay)}</span>
                        </>
                      )
                    ) : (
                      child.timestamp != null && child.timestamp > 0 && (
                        <>
                          <span>•</span>
                          <span>{formatGameDay(child.timestamp, currentGameDay)}</span>
                        </>
                      )
                    )}
                  </div>
                </div>

                <div className="flex items-center gap-[0.5rem] flex-shrink-0">
                  <AppTooltip content="View Child Details">
                    <button className="p-[0.25rem] text-foreground-muted hover:text-accent cursor-pointer">
                      <ChevronRight className="w-[1.25em] h-[1.25em]" />
                    </button>
                  </AppTooltip>
                </div>
              </div>
            )
          })}
        </div>
      </div>
    </ScrollArea>
  )
}
