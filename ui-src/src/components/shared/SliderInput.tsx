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
  const stepStr = step.toString()
  const decimals = stepStr.includes('.') ? stepStr.split('.')[1].length : 0
  const roundToStep = (val: number) =>
    decimals > 0 ? Number(val.toFixed(decimals)) : Math.round(val)
  const displayValue = roundToStep(value)

  return (
    <div className={`flex items-center gap-[0.75rem] ${className} ${disabled ? 'opacity-40' : ''}`}>
      <Slider
        value={[displayValue]}
        min={min}
        max={max}
        step={step}
        disabled={disabled}
        onValueChange={(vals) => onChange(roundToStep(vals[0]))}
        className="flex-1 w-[6rem]"
      />
      <span className="text-xs font-mono font-semibold text-accent-bright min-w-[2.5rem] text-right">
        {displayValue}
        {unit}
      </span>
    </div>
  )
}
