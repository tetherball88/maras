import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { PLAYER_FORM_ID } from '../../../data/mockData'
import { Users, Lock } from 'lucide-react'
import { areSameFormId } from '../../../utils/statusAvailability'
import { getParentRole } from '../../../utils/childUtils'

interface ChildGuardiansSectionProps {
  npcId: string
  className?: string
}

export const ChildGuardiansSection: React.FC<ChildGuardiansSectionProps> = ({
  npcId,
  className = '',
}) => {
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const children = useMarasStore((s) => s.children)
  const setPaternitySecretEdges = useMarasStore((s) => s.setPaternitySecretEdges)

  // Parent resolution for child NPCs
  const childRecord = children.get(npcId)

  const isSecretForPartner = Boolean(childRecord?.isSecretForPartner ?? childRecord?.isSecret)
  const isSecretForBio = Boolean(childRecord?.isSecretForBioFather)
  const hasActiveSecret = isSecretForPartner || isSecretForBio

  const handleToggleSecret = (type: 'partner' | 'bio', checked: boolean) => {
    if (!childRecord) return
    const currentPartner = isSecretForPartner
    const currentBio = isSecretForBio
    const newPartner = type === 'partner' ? checked : currentPartner
    const newBio = type === 'bio' ? checked : currentBio
    setPaternitySecretEdges(childRecord.childId, newBio, newPartner)
  }

  const playerName = npcProfiles.get(PLAYER_FORM_ID)?.name || 'Prisoner (Player)'
  const playerProfile = npcProfiles.get(PLAYER_FORM_ID)

  const parentAId = childRecord?.guardianAId || childRecord?.bioMotherId
  const parentBId = childRecord?.guardianBId || childRecord?.bioFatherId

  const parentAProfile = parentAId
    ? parentAId === PLAYER_FORM_ID
      ? { formId: PLAYER_FORM_ID, name: playerName, gender: playerProfile?.gender }
      : npcProfiles.get(parentAId)
    : null
  const parentBProfile = parentBId
    ? parentBId === PLAYER_FORM_ID
      ? { formId: PLAYER_FORM_ID, name: playerName, gender: playerProfile?.gender }
      : npcProfiles.get(parentBId)
    : null

  return (
    <SectionCard title="Parents & Guardians" icon={Users} className={className}>
      <div className="pt-[0.25rem] space-y-[0.75rem] text-xs">
        {(parentAProfile || parentBProfile) ? (
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-[0.75rem]">
            {/* Parent A */}
            {parentAProfile && (() => {
              const isPlayerParent = areSameFormId(parentAProfile.formId, PLAYER_FORM_ID)
              const { roleLabel, isSecret, isSecretForPartner: isSecretPartner, badgeStyle } = getParentRole(
                parentAProfile.formId,
                childRecord || {},
                parentAProfile.gender,
              )
              return (
                <div
                  onClick={() => !isPlayerParent && openNpcDetail(parentAProfile.formId)}
                  className={`p-[0.75rem] bg-panel-inset border border-border-subtle rounded flex items-center justify-between transition-colors ${isPlayerParent ? '' : 'hover:bg-interactive-hover cursor-pointer'
                    }`}
                >
                  <div>
                    <div className="flex items-center gap-[0.375rem] mb-[0.125rem]">
                      <span className={`text-[0.6875rem] ${badgeStyle || 'text-foreground-muted'} block font-medium`}>
                        {roleLabel}
                      </span>
                      {isSecret && (
                        <span
                          className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-purple-500/20 text-purple-300 border border-purple-500/30 font-semibold uppercase tracking-wider"
                          title={isSecretPartner ? 'Partner believes the child is theirs' : 'Secret from parent'}
                        >
                          {isSecretPartner ? 'Secret from Partner' : 'Secret'}
                        </span>
                      )}
                    </div>
                    <span className="font-bold text-accent-bright text-xs">{parentAProfile.name}</span>
                  </div>
                  {!isPlayerParent && (
                    <span className="text-[0.625rem] text-foreground-muted">View Profile →</span>
                  )}
                </div>
              )
            })()}

            {/* Parent B */}
            {parentBProfile && (() => {
              const isPlayerParent = areSameFormId(parentBProfile.formId, PLAYER_FORM_ID)
              const { roleLabel, isSecret, isSecretForPartner: isSecretPartner, badgeStyle } = getParentRole(
                parentBProfile.formId,
                childRecord || {},
                parentBProfile.gender,
              )
              return (
                <div
                  onClick={() => !isPlayerParent && openNpcDetail(parentBProfile.formId)}
                  className={`p-[0.75rem] bg-panel-inset border border-border-subtle rounded flex items-center justify-between transition-colors ${isPlayerParent ? '' : 'hover:bg-interactive-hover cursor-pointer'
                    }`}
                >
                  <div>
                    <div className="flex items-center gap-[0.375rem] mb-[0.125rem]">
                      <span className={`text-[0.6875rem] ${badgeStyle || 'text-foreground-muted'} block font-medium`}>
                        {roleLabel}
                      </span>
                      {isSecret && (
                        <span
                          className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-purple-500/20 text-purple-300 border border-purple-500/30 font-semibold uppercase tracking-wider"
                          title={isSecretPartner ? 'Partner believes the child is theirs' : 'Secret from parent'}
                        >
                          {isSecretPartner ? 'Secret from Partner' : 'Secret'}
                        </span>
                      )}
                    </div>
                    <span className="font-bold text-accent-bright text-xs">{parentBProfile.name}</span>
                  </div>
                  {!isPlayerParent && (
                    <span className="text-[0.625rem] text-foreground-muted">View Profile →</span>
                  )}
                </div>
              )
            })()}
          </div>
        ) : (
          <div className="text-foreground-muted italic py-[0.5rem]">No registered parents found for this child.</div>
        )}

        {/* Biological Parents (if distinct from current guardians) */}
        {((childRecord?.isAdopted !== false && (childRecord?.bioMotherId || childRecord?.bioFatherId)) ||
          (childRecord?.isAdopted === false && (
            Boolean(childRecord.bioMotherId && !areSameFormId(childRecord.bioMotherId, parentAId) && !areSameFormId(childRecord.bioMotherId, parentBId)) ||
            Boolean(childRecord.bioFatherId && !areSameFormId(childRecord.bioFatherId, parentAId) && !areSameFormId(childRecord.bioFatherId, parentBId))
          ))) && (
            <div className="pt-[0.5rem] border-t border-border-divider">
              <span className="text-[0.6875rem] font-medium text-foreground-muted block mb-[0.375rem]">
                Biological Parents
              </span>
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-[0.5rem]">
                {childRecord?.bioMotherName && (() => {
                  const isPlayerBioMom = Boolean(childRecord.bioMotherId && areSameFormId(childRecord.bioMotherId, PLAYER_FORM_ID))
                  return (
                    <div
                      onClick={() => childRecord.bioMotherId && !isPlayerBioMom && openNpcDetail(childRecord.bioMotherId)}
                      className={`p-[0.5rem] bg-panel-raised border border-border-subtle rounded flex items-center justify-between transition-colors ${isPlayerBioMom ? '' : 'hover:border-border-accent cursor-pointer'
                        }`}
                    >
                      <span className="text-foreground-primary font-medium">{childRecord.bioMotherName}</span>
                      <span className="text-[0.625rem] text-accent font-medium">
                        Biological Mother
                      </span>
                    </div>
                  )
                })()}
                {childRecord?.bioFatherName && (() => {
                  const isPlayerBioDad = Boolean(childRecord.bioFatherId && areSameFormId(childRecord.bioFatherId, PLAYER_FORM_ID))
                  return (
                    <div
                      onClick={() => childRecord.bioFatherId && !isPlayerBioDad && openNpcDetail(childRecord.bioFatherId)}
                      className={`p-[0.5rem] bg-panel-raised border border-border-subtle rounded flex items-center justify-between transition-colors ${isPlayerBioDad ? '' : 'hover:border-border-accent cursor-pointer'
                        }`}
                    >
                      <span className="text-foreground-primary font-medium">{childRecord.bioFatherName}</span>
                      <div className="flex items-center gap-[0.25rem]">
                        <span className="text-[0.625rem] text-accent font-medium">
                          Biological Father
                        </span>
                        {childRecord.isSecretForBioFather && (
                          <span className="text-[0.5625rem] px-[0.25rem] py-[0.0625rem] rounded bg-indigo-500/20 text-indigo-300 border border-indigo-500/30 font-semibold uppercase tracking-wider" title="Biological father is unaware that he fathered this child">
                            Secret From Him
                          </span>
                        )}
                      </div>
                    </div>
                  )
                })()}
              </div>
            </div>
          )}

        {/* Paternity Secrecy Manual Controls */}
        {hasActiveSecret && (
          <div className="pt-[0.625rem] border-t border-border-divider space-y-[0.5rem]">
            <div className="flex items-center justify-between">
              <span className="text-[0.6875rem] font-semibold text-foreground-muted uppercase tracking-wider flex items-center gap-[0.375rem]">
                <Lock className="w-[0.875em] h-[0.875em] text-purple-400" />
                Paternity Secrecy Controls
              </span>
              <span className="text-[0.625rem] text-foreground-muted">Manual Control</span>
            </div>

            <div className={isSecretForPartner && isSecretForBio ? "grid grid-cols-1 sm:grid-cols-2 gap-[0.5rem]" : "grid grid-cols-1 gap-[0.5rem]"}>
              {/* Secret from Partner */}
              {isSecretForPartner && (
                <label className="flex items-start gap-[0.625rem] p-[0.625rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded cursor-pointer transition-colors select-none group">
                  <input
                    type="checkbox"
                    checked={Boolean(childRecord?.isSecretForPartner ?? childRecord?.isSecret)}
                    onChange={(e) => handleToggleSecret('partner', e.target.checked)}
                    className="mt-[0.125rem] h-4 w-4 rounded border-border-divider text-purple-500 focus:ring-purple-500/20 accent-purple-500 cursor-pointer flex-shrink-0"
                  />
                  <div className="text-left space-y-[0.125rem]">
                    <span className="text-xs font-semibold text-foreground-primary group-hover:text-purple-300 transition-colors block">
                      Secret from Partner
                    </span>
                    <span className="text-[0.6875rem] text-foreground-muted block leading-tight">
                      Partner believes the child is theirs; true biological parentage is hidden from them.
                    </span>
                  </div>
                </label>
              )}

              {/* Secret from Biological Father */}
              {isSecretForBio && (
                <label className="flex items-start gap-[0.625rem] p-[0.625rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded cursor-pointer transition-colors select-none group">
                  <input
                    type="checkbox"
                    checked={Boolean(childRecord?.isSecretForBioFather)}
                    onChange={(e) => handleToggleSecret('bio', e.target.checked)}
                    className="mt-[0.125rem] h-4 w-4 rounded border-border-divider text-indigo-500 focus:ring-indigo-500/20 accent-indigo-500 cursor-pointer flex-shrink-0"
                  />
                  <div className="text-left space-y-[0.125rem]">
                    <span className="text-xs font-semibold text-foreground-primary group-hover:text-indigo-300 transition-colors block">
                      Secret from Biological Father
                    </span>
                    <span className="text-[0.6875rem] text-foreground-muted block leading-tight">
                      Biological father is unaware that he fathered this child.
                    </span>
                  </div>
                </label>
              )}
            </div>
          </div>
        )}
      </div>
    </SectionCard>
  )
}
