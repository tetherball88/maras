import React from 'react'
import { Input } from '@/components/ui/input'
import { Search } from 'lucide-react'
import { useTranslation } from '../../i18n'

interface SearchInputProps {
  value: string
  placeholder?: string
  onChange: (val: string) => void
  className?: string
}

export const SearchInput: React.FC<SearchInputProps> = ({
  value,
  placeholder,
  onChange,
  className = '',
}) => {
  const { t } = useTranslation()
  const resolvedPlaceholder = placeholder ?? t('shared.searchPlaceholder')

  return (
    <div className={`relative w-full ${className}`}>
      <Search className="w-[0.875em] h-[0.875em] text-foreground-muted absolute left-[0.5rem] top-1/2 -translate-y-1/2 z-10" />
      <Input
        type="text"
        placeholder={resolvedPlaceholder}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="pl-[1.75rem]"
      />
    </div>
  )
}
