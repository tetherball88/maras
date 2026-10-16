import React from 'react'
import { Slider } from '@/components/ui/slider'

interface SliderInputProps {
  value: number
  min: number
  max: number
  step?: number
  unit?: string
  disabled?: boolean
  onChange: (val: number) => void
  className?: string
}

export const SliderInput: React.FC<SliderInputProps> = ({
  value,
  min,
  max,
  step = 1,
  unit = '',
  disabled = false,
  onChange,
  className = '',
}) => {
  return (
    <div className={`flex items-center gap-[0.75rem] ${className} ${disabled ? 'opacity-40' : ''}`}>
      <Slider
        value={[value]}
        min={min}
        max={max}
        step={step}
        disabled={disabled}
        onValueChange={(vals) => onChange(vals[0])}
        className="flex-1 w-[6rem]"
      />
      <span className="text-xs font-mono font-semibold text-accent-bright min-w-[2.5rem] text-right">
        {value}
        {unit}
      </span>
    </div>
  )
}
