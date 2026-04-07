import { Monitor, Ban } from "lucide-react";

const activations = [
  { user: "usr_3829", machine: "DESKTOP-A1B2C3", os: "Windows 11", activated: "2026-03-01", status: "active" },
  { user: "usr_3829", machine: "LAPTOP-X9Y8Z7", os: "Windows 11", activated: "2026-03-05", status: "active" },
  { user: "usr_1204", machine: "STUDIO-PC-01", os: "Windows 10", activated: "2026-02-20", status: "active" },
  { user: "usr_5512", machine: "DESKTOP-REVOKED", os: "Windows 11", activated: "2026-01-15", status: "revoked" },
];

const ActivationsPage = () => (
  <div className="space-y-6">
    <div>
      <h2 className="text-2xl font-bold">Device Activations</h2>
      <p className="text-muted-foreground text-sm mt-1">Monitor and manage machine activations</p>
    </div>

    <div className="glass-panel overflow-hidden">
      <table className="w-full">
        <thead>
          <tr className="border-b border-border/50">
            {["User", "Machine ID", "OS", "Activated", "Status", "Actions"].map((h) => (
              <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {activations.map((a, i) => (
            <tr key={i} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
              <td className="px-4 py-3 text-sm font-mono text-highlight-silver">{a.user}</td>
              <td className="px-4 py-3 text-sm font-mono">{a.machine}</td>
              <td className="px-4 py-3 text-sm">{a.os}</td>
              <td className="px-4 py-3 text-sm text-muted-foreground">{a.activated}</td>
              <td className="px-4 py-3">
                <span className={`px-2 py-1 rounded-md text-xs font-medium ${a.status === "active" ? "bg-secondary/20 text-secondary" : "bg-muted text-muted-foreground"}`}>
                  {a.status}
                </span>
              </td>
              <td className="px-4 py-3">
                <button className="flex items-center gap-1 text-xs text-destructive hover:text-destructive/80">
                  <Ban className="w-3 h-3" /> Revoke
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  </div>
);

export default ActivationsPage;
