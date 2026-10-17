import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import {
  RelationshipStatus,
  SocialClass,
  SkillType,
  Temperament,
} from '../../types'
import { PLAYER_FORM_ID } from '../../data/mockData'
import { StatusBadge, getStatusLabel } from '../shared/StatusBadge'
import { formatGameDay } from '../../utils/dateUtils'
import { SliderInput } from '../shared/SliderInput'
import { SelectInput } from '../shared/SelectInput'
import { SectionCard } from '../shared/SectionCard'
import {
  SocialClassIcon,
  SkillTypeIcon,
  TemperamentIcon,
  NpcTraitBadges,
} from '../shared/TraitIcons'
import { ScrollArea } from '@/components/ui/scroll-area'
import { X, Heart, Home, Shield, Award, Clock, Users, Flame } from 'lucide-react'

export const PartnerDetailPanel: React.FC = () => {
  const selectedNpcId = useMarasStore((s) => s.selectedNpcId)
  const setSelectedNpcId = useMarasStore((s) => s.setSelectedNpcId)
  const detailPanelOpen = useMarasStore((s) => s.detailPanelOpen)

  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const relationships = useMarasStore((s) => s.relationships)
  const affectionData = useMarasStore((s) => s.affectionData)
  const hierarchyRanks = useMarasStore((s) => s.hierarchyRanks)
  const playerHomes = useMarasStore((s) => s.playerHomes)
  const currentGameDay = useMarasStore((s) => s.currentGameDay)

  const updateAffection = useMarasStore((s) => s.updateAffection)
  const updateNPCProfile = useMarasStore((s) => s.updateNPCProfile)
  const setHierarchyRank = useMarasStore((s) => s.setHierarchyRank)

  const isOpen = detailPanelOpen && Boolean(selectedNpcId)
  const npc = selectedNpcId ? npcProfiles.get(selectedNpcId) : null

  // Find relationship with player
  const rel = selectedNpcId
    ? relationships.find(
      (r) =>
        (r.actorA === PLAYER_FORM_ID && r.actorB === selectedNpcId) ||
        (r.actorA === selectedNpcId && r.actorB === PLAYER_FORM_ID),
    )
    : undefined

  const affectionObj = selectedNpcId ? affectionData.get(selectedNpcId) : undefined
  const currentAffection = affectionObj?.permanent ?? 0
  const status = rel?.status ?? RelationshipStatus.Candidate

  // Find other partners (relationships where selectedNpcId is involved with non-player)
  const otherRelationships = selectedNpcId
    ? relationships.filter(
      (r) =>
        (r.actorA === selectedNpcId && r.actorB !== PLAYER_FORM_ID) ||
        (r.actorB === selectedNpcId && r.actorA !== PLAYER_FORM_ID),
    )
    : []

  const currentRank = selectedNpcId ? hierarchyRanks.indexOf(selectedNpcId) : -1

  // Marriage difficulty calculation helper
  const calcChance = () => {
    if (status === RelationshipStatus.Married) return 1.0
    return Math.min(1.0, (currentAffection + 20) / 100)
  }

  const chanceVal = calcChance()
  let chanceText = 'Refuse'
  if (chanceVal >= 0.95) chanceText = 'Fated'
  else if (chanceVal >= 0.8) chanceText = 'Eager'
  else if (chanceVal >= 0.6) chanceText = 'Probably'
  else if (chanceVal >= 0.45) chanceText = 'On the fence'
  else if (chanceVal >= 0.25) chanceText = 'Unlikely'

  const availableHomes = [
    { value: 'Unset', label: 'Unset (No assigned house)' },
    ...playerHomes.map((h) => ({
      value: h.name,
      label: h.locationName ? `${h.name} (${h.locationName})` : h.name,
    })),
  ]

  const rankOptions = [
    { value: 'none', label: 'Unranked (4th+)' },
    { value: '0', label: '1st Rank Spouse' },
    { value: '1', label: '2nd Rank Spouse' },
    { value: '2', label: '3rd Rank Spouse' },
  ]

  const isPlayer = selectedNpcId === PLAYER_FORM_ID

  const enumToOptions = (e: Record<string, string | number>) =>
    Object.keys(e)
      .filter((k) => isNaN(Number(k)))
      .map((key, idx) => ({ value: idx, label: key }))

  return (
    <div
      className={`absolute top-0 right-0 bottom-0 z-40 w-[22rem] bg-panel border-l border-border-strong flex flex-col h-full overflow-hidden shadow-window transition-transform duration-300 ease-in-out ${isOpen ? 'translate-x-0' : 'translate-x-full pointer-events-none'
        }`}
    >
      {npc && (
        <>
          {/* Panel Header */}
          <header className="p-[1.0rem] bg-panel-raised border-b border-border-divider flex items-center justify-between">
            <div>
              <h2 className="text-lg font-bold text-accent-bright flex items-center gap-[0.375rem]">
                <span>{npc.name}</span>
                <span className="text-xs font-mono font-normal text-foreground-muted bg-panel-inset px-[0.375rem] py-[0.125rem] rounded border border-border-subtle">
                  {npc.formId}
                </span>
              </h2>
              <div className="text-xs text-foreground-secondary flex items-center gap-[0.5rem] mt-[0.125rem]">
                <span>{npc.race}</span>
                <span>•</span>
                <StatusBadge status={status} />
                <span>•</span>
                <NpcTraitBadges npc={npc} size="lg" showIconOnly />
              </div>
            </div>
            <button
              onClick={() => setSelectedNpcId(null)}
              className="text-foreground-muted hover:text-accent p-[0.25rem] rounded hover:bg-accent-soft cursor-pointer transition-colors"
            >
              <X className="w-[1.25em] h-[1.25em]" />
            </button>
          </header>

          {/* Content Sections */}
          <ScrollArea className="flex-1 p-[1.0rem]">
            <div className="space-y-[1.25rem] pr-[0.5rem]">
              {/* Section 1: Affection */}
              {!isPlayer && (
                <SectionCard
                  title="Affection Rating"
                  icon={Heart}
                  subtitle={`${Math.round(currentAffection)}%`}
                >
                  <div className="space-y-[0.5rem] pt-[0.25rem]">
                    <SliderInput
                      value={currentAffection}
                      min={0}
                      max={100}
                      unit="%"
                      onChange={(val) => selectedNpcId && updateAffection(selectedNpcId, val)}
                      className="w-full"
                    />

                    <div className="flex items-center justify-between text-[0.6875rem] text-foreground-muted">
                      <span>Decay Mult: {Number((affectionObj?.decayMultiplier ?? 1.0).toFixed(2))}x</span>
                      <span>
                        Last Seen:{' '}
                        {affectionObj?.daysSinceLastInteraction != null
                          ? Math.round(affectionObj.daysSinceLastInteraction) === 0
                            ? 'Today'
                            : `${Math.round(affectionObj.daysSinceLastInteraction)} days ago`
                          : 'Unknown'}
                      </span>
                    </div>
                  </div>
                </SectionCard>
              )}

              {/* Section 2: Hierarchy Rank (Married Only) */}
              {!isPlayer && status === RelationshipStatus.Married && (
                <SectionCard
                  title="Spouse Hierarchy Rank"
                  icon={Award}
                  subtitle={currentRank >= 0 ? `${currentRank + 1}st Rank` : 'Unranked'}
                >
                  <div className="pt-[0.25rem]">
                    <SelectInput
                      value={currentRank === -1 ? 'none' : currentRank.toString()}
                      options={rankOptions}
                      onChange={(val) =>
                        selectedNpcId && setHierarchyRank(selectedNpcId, val === 'none' ? -1 : parseInt(val))
                      }
                      className="w-full"
                    />
                  </div>
                </SectionCard>
              )}

              {/* Section 3: Profile Attributes */}
              <SectionCard title="Profile Characteristics" icon={Shield}>
                <div className="pt-[0.25rem] space-y-[0.5rem]">
                  <div className="grid grid-cols-2 gap-[0.5rem] text-xs">
                    <div>
                      <label className="text-foreground-muted block mb-[0.125rem]">Social Class</label>
                      <SelectInput
                        value={npc.socialClass}
                        options={Object.keys(SocialClass)
                          .filter((k) => isNaN(Number(k)))
                          .map((key, idx) => ({
                            value: idx,
                            label: key,
                            icon: <SocialClassIcon value={idx} size="md" showTooltip={false} />,
                          }))}
                        onChange={(val) =>
                          selectedNpcId && updateNPCProfile(selectedNpcId, { socialClass: parseInt(val) })
                        }
                        className="w-full"
                      />
                    </div>

                    <div>
                      <label className="text-foreground-muted block mb-[0.125rem]">Skill Type</label>
                      <SelectInput
                        value={npc.skillType}
                        options={Object.keys(SkillType)
                          .filter((k) => isNaN(Number(k)))
                          .map((key, idx) => ({
                            value: idx,
                            label: key,
                            icon: <SkillTypeIcon value={idx} size="md" showTooltip={false} />,
                          }))}
                        onChange={(val) =>
                          selectedNpcId && updateNPCProfile(selectedNpcId, { skillType: parseInt(val) })
                        }
                        className="w-full"
                      />
                    </div>

                    <div>
                      <label className="text-foreground-muted block mb-[0.125rem]">Temperament</label>
                      <SelectInput
                        value={npc.temperament}
                        options={Object.keys(Temperament)
                          .filter((k) => isNaN(Number(k)))
                          .map((key, idx) => ({
                            value: idx,
                            label: key,
                            icon: <TemperamentIcon value={idx} size="md" showTooltip={false} />,
                          }))}
                        onChange={(val) =>
                          selectedNpcId && updateNPCProfile(selectedNpcId, { temperament: parseInt(val) })
                        }
                        className="w-full"
                      />
                    </div>
                  </div>
                </div>
              </SectionCard>

              {/* Section 4: Home & Housing */}
              {!isPlayer && (
                <SectionCard title="Residence & Housing" icon={Home}>
                  <div className="space-y-[0.375rem] text-xs pt-[0.25rem]">
                    <div>
                      <label className="text-foreground-muted block mb-[0.125rem]">Assigned Player Home</label>
                      <SelectInput
                        value={npc.currentHome || 'Unset'}
                        options={availableHomes}
                        onChange={(val) =>
                          selectedNpcId &&
                          updateNPCProfile(selectedNpcId, { currentHome: val === 'Unset' ? undefined : val })
                        }
                        className="w-full"
                      />
                    </div>

                    <div className="flex justify-between text-[0.6875rem]">
                      <span className="text-foreground-muted">Original Home:</span>
                      <span className="text-foreground-secondary">{npc.originalHome || 'Unknown'}</span>
                    </div>
                  </div>
                </SectionCard>
              )}

              {/* Section 5: Cross-Partner Relationships */}
              {otherRelationships.length > 0 && (
                <SectionCard title="Other NPC Relationships" icon={Users}>
                  <div className="space-y-[0.25rem] text-xs pt-[0.25rem]">
                    {otherRelationships.map((r, i) => {
                      const partnerId = r.actorA === selectedNpcId ? r.actorB : r.actorA
                      const partnerName = npcProfiles.get(partnerId)?.name || 'Unknown NPC'
                      return (
                        <div
                          key={i}
                          className="flex justify-between items-center bg-panel p-[0.375rem] rounded border border-border-divider"
                        >
                          <span className="text-foreground-primary">{partnerName}</span>
                          <span className="text-accent text-[0.6875rem] font-medium">
                            {getStatusLabel(r.status)}
                          </span>
                        </div>
                      )
                    })}
                  </div>
                </SectionCard>
              )}

              {/* Section 6: Marriage Difficulty (if non-married) */}
              {status !== RelationshipStatus.Married && (
                <SectionCard
                  title="Proposal Acceptance Chance"
                  icon={Flame}
                  subtitle={`${Math.round(chanceVal * 100)}%`}
                >
                  <div className="text-[0.6875rem] text-foreground-secondary pt-[0.25rem]">
                    Status: <span className="font-semibold text-accent">{chanceText}</span>
                  </div>
                </SectionCard>
              )}

              {/* Section 7: Status History Timeline */}
              {rel && rel.statusHistory.length > 0 && (
                <SectionCard title="Status History Timeline" icon={Clock}>
                  <div className="space-y-[0.375rem] pt-[0.25rem]">
                    {rel.statusHistory.map((h, idx) => (
                      <div
                        key={idx}
                        className="flex items-center justify-between text-xs text-foreground-secondary"
                      >
                        <span>{formatGameDay(h.timestamp, currentGameDay)}</span>
                        <span>
                          {h.timestamp < 0 ? 'Pre-existing' : getStatusLabel(h.fromStatus)} →{' '}
                          <strong className="text-accent-bright">{getStatusLabel(h.toStatus)}</strong>
                        </span>
                      </div>
                    ))}
                  </div>
                </SectionCard>
              )}
            </div>
          </ScrollArea>
        </>
      )}
    </div>
  )
}
