import * as React from "react"
import { cva, type VariantProps } from "class-variance-authority"
import { cn } from "@/lib/utils"

const badgeVariants = cva(
  "inline-flex items-center rounded-full border px-[0.625rem] py-[0.125rem] text-xs font-semibold transition-colors focus:outline-none focus:ring-2 focus:ring-accent focus:ring-offset-2",
  {
    variants: {
      variant: {
        default:
          "border-border-accent bg-accent-soft text-accent-bright",
        secondary:
          "border-border-subtle bg-panel-raised text-foreground-secondary",
        outline:
          "border-border-subtle text-foreground-primary",
        success:
          "border-status-success bg-status-success-soft text-status-success",
        danger:
          "border-status-danger bg-status-danger-soft text-status-danger",
        warning:
          "border-status-warning bg-status-warning-soft text-status-warning",
      },
    },
    defaultVariants: {
      variant: "default",
    },
  }
)

export interface BadgeProps
  extends React.HTMLAttributes<HTMLDivElement>,
  VariantProps<typeof badgeVariants> { }

function Badge({ className, variant, ...props }: BadgeProps) {
  return (
    <div className={cn(badgeVariants({ variant }), className)} {...props} />
  )
}

export { Badge, badgeVariants }
