import React from 'react'
import { useMarasStore } from '../../../store/useMarasStore'
import { SectionCard } from '../../shared/SectionCard'
import { Cpu } from 'lucide-react'

interface AiPackageSectionProps {
  npcId: string
  className?: string
}

export const AiPackageSection: React.FC<AiPackageSectionProps> = ({
  npcId,
  className = '',
}) => {
  const npcProfiles = useMarasStore((s) => s.npcProfiles)
  const npc = npcProfiles.get(npcId)
  const currentPackage = npc?.currentPackage || 'MARAS_HomeSandboxPackage'

  return (
    <SectionCard title="AI Process Package" icon={Cpu} className={className}>
      <div className="pt-[0.25rem] space-y-[0.375rem] text-xs flex flex-col justify-between h-full">
        <div>
          <label className="text-foreground-muted block mb-[0.125rem]">
            Current Active AI Package
          </label>
          <div className="p-[0.5rem] bg-panel-inset border border-border-subtle rounded text-accent-bright font-mono text-[0.6875rem] flex items-center gap-[0.375rem]">
            <Cpu className="w-[1em] h-[1em] text-accent flex-shrink-0" />
            <span className="truncate" title={currentPackage}>
              {currentPackage}
            </span>
          </div>
        </div>
      </div>
    </SectionCard>
  )
}
