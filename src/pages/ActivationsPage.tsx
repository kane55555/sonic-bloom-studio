import { Ban, RotateCcw } from "lucide-react";
import { toast } from "sonner";
import { useDevices, useReactivateDevice, useRevokeDevice } from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";

const ActivationsPage = () => {
  const { data, isLoading, error } = useDevices();
  const revoke = useRevokeDevice();
  const reactivate = useReactivateDevice();
  const rows = data ?? [];

  const onRevoke = (id: string) =>
    revoke.mutate(id, {
      onSuccess: () => toast.success("Device revoked"),
      onError: (e) => toast.error(e instanceof Error ? e.message : "Failed"),
    });

  const onReactivate = (id: string) =>
    reactivate.mutate(id, {
      onSuccess: () => toast.success("Device reactivated"),
      onError: (e) => toast.error(e instanceof Error ? e.message : "Failed"),
    });

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold">Device Activations</h2>
        <p className="text-muted-foreground text-sm mt-1">Monitor and manage machine activations</p>
      </div>

      <AsyncBoundary
        isLoading={isLoading}
        error={error}
        isEmpty={!isLoading && !error && rows.length === 0}
        emptyMessage="No devices have activated yet."
      >
        <div className="glass-panel overflow-hidden">
          <table className="w-full">
            <thead>
              <tr className="border-b border-border/50">
                {["Email", "Machine (hash)", "OS", "Activated", "Last seen", "Status", "Actions"].map((h) => (
                  <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((d) => (
                <tr key={d.id} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
                  <td className="px-4 py-3 text-sm">{d.email ?? "—"}</td>
                  <td className="px-4 py-3 text-xs font-mono text-highlight-silver" title={d.machine_id}>
                    {d.machine_id.slice(0, 12)}…
                  </td>
                  <td className="px-4 py-3 text-sm">{d.os_info ?? "—"}</td>
                  <td className="px-4 py-3 text-sm text-muted-foreground">{d.activated_at.slice(0, 10)}</td>
                  <td className="px-4 py-3 text-sm text-muted-foreground">{d.last_seen_at.slice(0, 10)}</td>
                  <td className="px-4 py-3">
                    <span className={`px-2 py-1 rounded-md text-xs font-medium ${d.is_active ? "bg-secondary/20 text-secondary" : "bg-muted text-muted-foreground"}`}>
                      {d.is_active ? "active" : "revoked"}
                    </span>
                  </td>
                  <td className="px-4 py-3">
                    {d.is_active ? (
                      <button
                        disabled={revoke.isPending}
                        onClick={() => onRevoke(d.id)}
                        className="flex items-center gap-1 text-xs text-destructive hover:text-destructive/80"
                      >
                        <Ban className="w-3 h-3" /> Revoke
                      </button>
                    ) : (
                      <button
                        disabled={reactivate.isPending}
                        onClick={() => onReactivate(d.id)}
                        className="flex items-center gap-1 text-xs text-secondary hover:text-secondary/80"
                      >
                        <RotateCcw className="w-3 h-3" /> Reactivate
                      </button>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </AsyncBoundary>
    </div>
  );
};

export default ActivationsPage;
