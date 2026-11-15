import React, { useRef } from 'react'
import { useVirtualizer } from '@tanstack/react-virtual'

export interface VirtualListProps<T> {
  items: T[]
  estimateSize?: number
  overscan?: number
  getItemKey?: (item: T, index: number) => string | number
  renderItem: (item: T, index: number) => React.ReactNode
  emptyMessage?: string
  renderEmpty?: () => React.ReactNode
  className?: string
  itemClassName?: string
}

export function VirtualList<T>({
  items,
  estimateSize = 52,
  overscan = 6,
  getItemKey,
  renderItem,
  emptyMessage = 'No matching items found.',
  renderEmpty,
  className = 'flex-1 min-h-0 overflow-y-auto p-[0.5rem]',
  itemClassName = 'pb-[0.25rem]',
}: VirtualListProps<T>) {
  const parentRef = useRef<HTMLDivElement>(null)

  const rowVirtualizer = useVirtualizer({
    count: items.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => estimateSize,
    overscan,
  })

  if (items.length === 0) {
    if (renderEmpty) {
      return <div className={className}>{renderEmpty()}</div>
    }
    return (
      <div className={className}>
        <div className="text-center py-[2rem] text-xs text-foreground-muted">
          {emptyMessage}
        </div>
      </div>
    )
  }

  return (
    <div ref={parentRef} className={className}>
      <div
        style={{
          height: `${rowVirtualizer.getTotalSize()}px`,
          width: '100%',
          position: 'relative',
        }}
      >
        {rowVirtualizer.getVirtualItems().map((virtualRow) => {
          const item = items[virtualRow.index]
          const key = getItemKey
            ? getItemKey(item, virtualRow.index)
            : (item as any)?.formId || (item as any)?.id || virtualRow.index

          return (
            <div
              key={key}
              data-index={virtualRow.index}
              ref={rowVirtualizer.measureElement}
              style={{
                position: 'absolute',
                top: 0,
                left: 0,
                width: '100%',
                transform: `translateY(${virtualRow.start}px)`,
              }}
              className={itemClassName}
            >
              {renderItem(item, virtualRow.index)}
            </div>
          )
        })}
      </div>
    </div>
  )
}
