import { Shield, Clock } from "lucide-react";
import { useState } from "react";
import { useAuditLogs } from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";
import type { AuditLog } from "@/lib/admin/schemas";

const EVENT_TYPES: AuditLog["event_type"][] = [
  "license_activated", "license_verified", "license_failed",
  "device_revoked", "subscription_updated", "entitlement_granted",
  "pack_published", "pack_unpublished", "admin_action",
  "login_success", "login_failed",
];

const SecurityPage = () => {
  const [filter, setFilter] = useState<AuditLog["event_type"] | "">("");
  const { data, isLoading, error } = useAuditLogs(
    filter ? { eventType: filter, limit: 200 } : { limit: 200 },
  );
  const rows = data ?? [];

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold">Security & Audit</h2>
        <p className="text-muted-foreground text-sm mt-1">License events and security audit trail</p>
      </div>

      <div className="flex items-center gap-3">
        <select
          value={filter}
          onChange={(e) => setFilter(e.target.value as AuditLog["event_type"] | "")}
          className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
          <option value="">All event types</option>
          {EVENT_TYPES.map((t) => <option key={t} value={t}>{t}</option>)}
        </select>
      </div>

      <AsyncBoundary
        isLoading={isLoading}
        error={error}
        isEmpty={!isLoading && !error && rows.length === 0}
        emptyMessage="No audit events recorded yet."
      >
        <div className="glass-panel overflow-hidden">
          <div className="flex items-center gap-2 p-4 border-b border-border/50">
            <Shield className="w-4 h-4 text-primary" />
            <h3 className="font-semibold text-sm">Audit Log</h3>
          </div>
          <table className="w-full">
            <thead>
              <tr className="border-b border-border/50">
                {["Event", "User", "Machine", "IP", "Timestamp"].map((h) => (
                  <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((e) => (
                <tr key={e.id} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
                  <td className="px-4 py-3 text-sm font-mono">{e.event_type}</td>
                  <td className="px-4 py-3 text-sm font-mono text-highlight-silver">{e.user_id?.slice(0, 8) ?? "—"}</td>
                  <td className="px-4 py-3 text-xs font-mono text-muted-foreground">{e.machine_id ? e.machine_id.slice(0, 12) + "…" : "—"}</td>
                  <td className="px-4 py-3 text-sm text-muted-foreground">{e.ip_address ?? "—"}</td>
                  <td className="px-4 py-3 text-sm text-muted-foreground flex items-center gap-1">
                    <Clock className="w-3 h-3" /> {e.created_at.replace("T", " ").slice(0, 16)}
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

export default SecurityPage;
