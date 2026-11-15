import React from 'react'
import { Home, AlertCircle, X, Check, Users, ArrowRight } from 'lucide-react'

interface ChildRelocationModalProps {
  isOpen: boolean
  onClose: () => void
  onConfirm: () => void
  childName: string
  targetHomeName: string
  spouseName?: string | null
  otherChildrenNames: string[]
}

export const ChildRelocationModal: React.FC<ChildRelocationModalProps> = ({
  isOpen,
  onClose,
  onConfirm,
  childName,
  targetHomeName,
  spouseName,
  otherChildrenNames,
}) => {
  if (!isOpen) return null

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 backdrop-blur-sm p-[1rem] animate-in fade-in duration-200">
      <div
        className="bg-panel border border-border-accent/40 rounded-xl shadow-2xl max-w-md w-full flex flex-col overflow-hidden text-foreground-primary font-ui"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-[1.25rem] border-b border-border-divider flex items-center justify-between bg-panel-raised">
          <div className="flex items-center gap-[0.75rem]">
            <div className="p-[0.5rem] rounded-full bg-accent-soft text-accent border border-border-accent">
              <Home className="w-[1.25rem] h-[1.25rem]" />
            </div>
            <div>
              <h2 className="text-sm font-bold text-accent-bright">
                Confirm Household Relocation
              </h2>
              <p className="text-[0.6875rem] text-foreground-muted">
                Moving family unit to {targetHomeName}
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-[0.375rem] text-foreground-muted hover:text-foreground-primary hover:bg-panel-inset rounded-lg transition-colors cursor-pointer"
            title="Cancel"
          >
            <X className="w-[1.25rem] h-[1.25rem]" />
          </button>
        </div>

        {/* Content */}
        <div className="p-[1.25rem] space-y-[1rem] text-xs">
          <div className="p-[0.75rem] bg-amber-500/10 border border-amber-500/30 rounded-lg text-amber-300 flex items-start gap-[0.625rem]">
            <AlertCircle className="w-[1.25rem] h-[1.25rem] text-amber-400 flex-shrink-0 mt-[0.125rem]" />
            <div className="space-y-[0.25rem]">
              <span className="font-semibold block text-xs">Family Unit Relocation</span>
              <p className="text-[0.6875rem] text-amber-200/90 leading-relaxed">
                Relocating <span className="font-bold text-white">{childName}</span> will also move their co-parent and siblings to maintain household cohesion.
              </p>
            </div>
          </div>

          <div className="space-y-[0.5rem]">
            <span className="text-[0.6875rem] font-semibold text-foreground-muted uppercase tracking-wider block">
              Affected Household Members
            </span>
            <div className="bg-panel-inset border border-border-subtle rounded-lg p-[0.75rem] space-y-[0.5rem]">
              {/* Primary Child */}
              <div className="flex items-center justify-between text-xs">
                <span className="text-foreground-muted">Selected Child:</span>
                <span className="font-semibold text-accent-bright">{childName}</span>
              </div>

              {/* Spouse */}
              {spouseName && (
                <div className="flex items-center justify-between text-xs pt-[0.375rem] border-t border-border-divider/50">
                  <span className="text-foreground-muted">Co-Parent Spouse:</span>
                  <span className="font-semibold text-accent-bright">{spouseName}</span>
                </div>
              )}

              {/* Other Children */}
              {otherChildrenNames.length > 0 && (
                <div className="flex items-start justify-between text-xs pt-[0.375rem] border-t border-border-divider/50">
                  <span className="text-foreground-muted">Sibling(s):</span>
                  <div className="text-right">
                    {otherChildrenNames.map((name, i) => (
                      <div key={i} className="font-semibold text-accent-bright">{name}</div>
                    ))}
                  </div>
                </div>
              )}

              {/* Destination */}
              <div className="flex items-center justify-between text-xs pt-[0.375rem] border-t border-border-divider/50">
                <span className="text-foreground-muted flex items-center gap-[0.25rem]">
                  <span>New Residence:</span>
                </span>
                <span className="font-bold text-status-success flex items-center gap-[0.25rem]">
                  <ArrowRight className="w-[0.875em] h-[0.875em]" />
                  <span>{targetHomeName}</span>
                </span>
              </div>
            </div>
          </div>
        </div>

        {/* Footer */}
        <div className="p-[1rem] bg-panel-raised border-t border-border-divider flex items-center justify-end gap-[0.5rem]">
          <button
            onClick={onClose}
            className="px-[0.875rem] py-[0.375rem] bg-panel-inset hover:bg-interactive-hover border border-border-subtle rounded text-foreground-secondary text-xs font-semibold transition-colors cursor-pointer"
          >
            Cancel
          </button>
          <button
            onClick={onConfirm}
            className="px-[1rem] py-[0.375rem] bg-accent hover:bg-accent-bright text-accent-contrast rounded text-xs font-bold shadow-lg shadow-accent/20 flex items-center gap-[0.375rem] transition-colors cursor-pointer"
          >
            <Check className="w-[1em] h-[1em]" />
            <span>Confirm Relocation</span>
          </button>
        </div>
      </div>
    </div>
  )
}
