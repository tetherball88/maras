import React, { Component, ErrorInfo, ReactNode } from 'react'
import { AlertTriangle, RefreshCw } from 'lucide-react'

interface Props {
  children: ReactNode
}

interface State {
  hasError: boolean
  error: Error | null
}

export class ErrorBoundary extends Component<Props, State> {
  public state: State = {
    hasError: false,
    error: null,
  }

  public static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error }
  }

  public componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error('Uncaught error in MARAS React tree:', error, errorInfo)
    window.log?.(`MARAS React Error: ${error.message}`)
  }

  private handleReload = () => {
    this.setState({ hasError: false, error: null })
    window.location.reload()
  }

  public render() {
    if (this.state.hasError) {
      return (
        <div className="fixed inset-0 bg-background flex items-center justify-center p-[2rem] select-none font-ui text-foreground-primary">
          <div className="bg-panel border border-border-danger/60 rounded-xl shadow-window p-[2rem] max-w-[32rem] flex flex-col items-center text-center gap-[1rem]">
            <div className="w-[3rem] h-[3rem] rounded-full bg-rose-950/60 border border-rose-800/80 flex items-center justify-center text-rose-400">
              <AlertTriangle className="w-[1.75rem] h-[1.75rem]" />
            </div>
            <div>
              <h2 className="text-base font-bold text-foreground-primary">An error occurred in M.A.R.A.S UI</h2>
              <p className="text-xs text-foreground-muted mt-[0.25rem] leading-relaxed">
                The UI encountered an unexpected error. You can reload the view to recover state.
              </p>
            </div>
            {this.state.error?.message && (
              <div className="w-full bg-panel-inset border border-border-subtle rounded-md p-[0.75rem] text-left">
                <p className="text-[0.6875rem] font-mono text-rose-400 break-words">
                  {this.state.error.message}
                </p>
              </div>
            )}
            <button
              onClick={this.handleReload}
              className="px-[1.25rem] py-[0.5rem] bg-accent hover:bg-accent-bright text-panel font-semibold text-xs rounded-lg transition-colors cursor-pointer flex items-center gap-[0.5rem]"
            >
              <RefreshCw className="w-[1rem] h-[1rem]" />
              <span>Reload UI</span>
            </button>
          </div>
        </div>
      )
    }

    return this.props.children
  }
}
