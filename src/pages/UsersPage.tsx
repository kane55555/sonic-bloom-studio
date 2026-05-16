import { Search, Filter, MoreHorizontal } from "lucide-react";
import { useState } from "react";
import { useUsers } from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";

const statusColor: Record<string, string> = {
  active: "bg-secondary/20 text-secondary",
  trialing: "bg-secondary/20 text-secondary",
  past_due: "bg-destructive/20 text-destructive",
  expired: "bg-destructive/20 text-destructive",
  canceled: "bg-muted text-muted-foreground",
};

const UsersPage = () => {
  const [q, setQ] = useState("");
  const { data, isLoading, error } = useUsers();

  const rows = (data ?? []).filter((u) =>
    q.trim() === "" ||
    (u.email ?? "").toLowerCase().includes(q.toLowerCase()) ||
    u.id.toLowerCase().includes(q.toLowerCase()),
  );

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold">Users</h2>
        <p className="text-muted-foreground text-sm mt-1">Manage registered users and their entitlements</p>
      </div>

      <div className="flex gap-3">
        <div className="flex-1 relative">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
          <input
            value={q}
            onChange={(e) => setQ(e.target.value)}
            className="w-full pl-10 pr-4 py-2.5 rounded-lg bg-muted border border-border text-sm text-foreground placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-primary"
            placeholder="Search by email or user id…"
          />
        </div>
        <button className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-muted border border-border text-sm text-foreground hover:bg-muted/80">
          <Filter className="w-4 h-4" /> Filter
        </button>
      </div>

      <AsyncBoundary
        isLoading={isLoading}
        error={error}
        isEmpty={!isLoading && !error && rows.length === 0}
        emptyMessage="No users yet."
      >
        <div className="glass-panel overflow-hidden">
          <table className="w-full">
            <thead>
              <tr className="border-b border-border/50">
                {["User ID", "Email", "Plan", "Status", "Devices", "Joined", ""].map((h) => (
                  <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((u) => (
                <tr key={u.id} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
                  <td className="px-4 py-3 text-sm font-mono text-highlight-silver">{u.id.slice(0, 8)}</td>
                  <td className="px-4 py-3 text-sm">{u.email}</td>
                  <td className="px-4 py-3 text-sm font-medium capitalize">{u.plan}</td>
                  <td className="px-4 py-3">
                    <span className={`px-2 py-1 rounded-md text-xs font-medium ${statusColor[u.subscription_status] ?? "bg-muted text-muted-foreground"}`}>
                      {u.subscription_status}
                    </span>
                  </td>
                  <td className="px-4 py-3 text-sm text-center">{u.device_count} / {u.max_devices}</td>
                  <td className="px-4 py-3 text-sm text-muted-foreground">{u.created_at.slice(0, 10)}</td>
                  <td className="px-4 py-3"><MoreHorizontal className="w-4 h-4 text-muted-foreground cursor-pointer hover:text-foreground" /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </AsyncBoundary>
    </div>
  );
};

export default UsersPage;
