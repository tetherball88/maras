import React from 'react'
import { useTranslation } from '../../../i18n'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { Crown } from 'lucide-react'

interface PlayerPolicySectionProps {
  className?: string
}

export const PlayerPolicySection: React.FC<PlayerPolicySectionProps> = ({
  className = '',
}) => {
  const { t, tp } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)

  return (
    <SectionCard
      title={t('npcDetail.playerPolicy.title')}
      icon={Crown}
      className={className}
    >
      <div className="pt-[0.25rem] space-y-[0.75rem] text-xs">
        <div className="flex justify-between items-center bg-panel-raised p-[0.625rem] rounded border border-border-subtle">
          <div>
            <div className="font-semibold text-foreground-primary">
              {t('npcDetail.playerPolicy.polygamyMode')}
            </div>
            <div className="text-[0.6875rem] text-foreground-muted">
              {t('npcDetail.playerPolicy.polygamyDesc')}
            </div>
          </div>
          <button
            onClick={() => updateSetting('enablePolygamy', !settings.enablePolygamy)}
            className={`px-[0.625rem] py-[0.25rem] rounded text-xs font-semibold border cursor-pointer transition-all ${settings.enablePolygamy
                ? 'bg-purple-500/20 text-purple-300 border-purple-500/40 hover:bg-purple-500/30'
                : 'bg-panel-inset text-foreground-muted border-border-divider hover:bg-interactive-hover'
              }`}
          >
            {settings.enablePolygamy
              ? t('npcDetail.playerPolicy.enabled')
              : t('npcDetail.playerPolicy.disabled')}
          </button>
        </div>

        <p className="text-[0.6875rem] text-foreground-muted italic">
          {t('npcDetail.playerPolicy.polygamyNote')}
        </p>
      </div>
    </SectionCard>
  )
}
