import React from 'react'

interface SettingRowProps {
  label: string
  description?: string
  disabled?: boolean
  children: React.ReactNode
}

export const SettingRow: React.FC<SettingRowProps> = ({
  label,
  description,
  disabled = false,
  children,
}) => {
  return (
    <div
      className={`flex items-center justify-between p-[0.75rem] border-b border-border-divider last:border-b-0 ${disabled ? 'opacity-50' : ''
        }`}
    >
      <div className="flex flex-col pr-[1rem]">
        <span className="text-xs font-semibold text-foreground-primary">{label}</span>
        {description && (
          <span className="text-[0.6875rem] text-foreground-muted mt-[0.125rem]">
            {description}
          </span>
        )}
      </div>

      <div className="flex-shrink-0">{children}</div>
    </div>
  )
}
