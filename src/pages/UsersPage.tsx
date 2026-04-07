import { Search, Filter, MoreHorizontal } from "lucide-react";

const mockUsers = [
  { id: "usr_3829", email: "producer1@email.com", plan: "Pro", status: "active", devices: 2, joined: "2025-11-12" },
  { id: "usr_1204", email: "beatmaker@email.com", plan: "Basic", status: "active", devices: 1, joined: "2025-12-01" },
  { id: "usr_0891", email: "studio@email.com", plan: "Pro", status: "expired", devices: 3, joined: "2025-09-20" },
  { id: "usr_5512", email: "dj_wave@email.com", plan: "Basic", status: "revoked", devices: 0, joined: "2026-01-15" },
  { id: "usr_7744", email: "keys@email.com", plan: "Pro", status: "active", devices: 2, joined: "2026-02-08" },
];

const statusColor: Record<string, string> = {
  active: "bg-secondary/20 text-secondary",
  expired: "bg-destructive/20 text-destructive",
  revoked: "bg-muted text-muted-foreground",
};

const UsersPage = () => (
  <div className="space-y-6">
    <div className="flex items-center justify-between">
      <div>
        <h2 className="text-2xl font-bold">Users</h2>
        <p className="text-muted-foreground text-sm mt-1">Manage registered users and their entitlements</p>
      </div>
    </div>

    <div className="flex gap-3">
      <div className="flex-1 relative">
        <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
        <input className="w-full pl-10 pr-4 py-2.5 rounded-lg bg-muted border border-border text-sm text-foreground placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-primary" placeholder="Search users..." />
      </div>
      <button className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-muted border border-border text-sm text-foreground hover:bg-muted/80">
        <Filter className="w-4 h-4" /> Filter
      </button>
    </div>

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
          {mockUsers.map((u) => (
            <tr key={u.id} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
              <td className="px-4 py-3 text-sm font-mono text-highlight-silver">{u.id}</td>
              <td className="px-4 py-3 text-sm">{u.email}</td>
              <td className="px-4 py-3 text-sm font-medium">{u.plan}</td>
              <td className="px-4 py-3"><span className={`px-2 py-1 rounded-md text-xs font-medium ${statusColor[u.status]}`}>{u.status}</span></td>
              <td className="px-4 py-3 text-sm text-center">{u.devices}</td>
              <td className="px-4 py-3 text-sm text-muted-foreground">{u.joined}</td>
              <td className="px-4 py-3"><MoreHorizontal className="w-4 h-4 text-muted-foreground cursor-pointer hover:text-foreground" /></td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  </div>
);

export default UsersPage;
