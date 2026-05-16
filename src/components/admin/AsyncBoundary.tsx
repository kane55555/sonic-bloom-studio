import { AlertCircle, Loader2, CloudOff } from "lucide-react";
import { CloudDisabledError, isCloudEnabled } from "@/lib/admin/supabaseClient";
import type { ReactNode } from "react";

interface Props {
  isLoading: boolean;
  error: unknown;
  isEmpty?: boolean;
  emptyMessage?: string;
  children: ReactNode;
}

/**
 * Uniform loading / error / cloud-disabled / empty wrapper used by every
 * admin page. Keeps page bodies free of conditional spaghetti.
 */
const AsyncBoundary = ({ isLoading, error, isEmpty, emptyMessage, children }: Props) => {
  if (!isCloudEnabled()) {
    return (
      <div className="glass-panel p-8 text-center text-muted-foreground">
        <CloudOff className="w-6 h-6 mx-auto mb-3 text-muted-foreground" />
        <p className="text-sm font-medium">Lovable Cloud is not enabled yet</p>
        <p className="text-xs mt-2">
          Live data, license functions, and admin mutations come online once
          Cloud is enabled and the migrations under
          <code className="mx-1 px-1 rounded bg-muted">backend/db/migrations</code>
          have been applied.
        </p>
      </div>
    );
  }
  if (isLoading) {
    return (
      <div className="glass-panel p-8 flex items-center justify-center gap-3 text-muted-foreground">
        <Loader2 className="w-4 h-4 animate-spin" />
        <span className="text-sm">Loading…</span>
      </div>
    );
  }
  if (error) {
    const isCloudOff = error instanceof CloudDisabledError;
    return (
      <div className="glass-panel p-6 flex items-start gap-3 text-destructive">
        <AlertCircle className="w-5 h-5 mt-0.5" />
        <div>
          <p className="text-sm font-medium">
            {isCloudOff ? "Cloud disabled" : "Failed to load"}
          </p>
          <p className="text-xs mt-1 text-muted-foreground break-all">
            {error instanceof Error ? error.message : String(error)}
          </p>
        </div>
      </div>
    );
  }
  if (isEmpty) {
    return (
      <div className="glass-panel p-8 text-center text-sm text-muted-foreground">
        {emptyMessage ?? "No records yet."}
      </div>
    );
  }
  return <>{children}</>;
};

export default AsyncBoundary;
