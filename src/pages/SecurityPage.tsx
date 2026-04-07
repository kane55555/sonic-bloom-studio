import { Shield, Clock } from "lucide-react";

const auditLog = [
  { event: "login_success", user: "usr_3829", ip: "192.168.1.x", time: "2026-04-07 10:23" },
  { event: "device_activated", user: "usr_1204", ip: "10.0.0.x", time: "2026-04-07 09:15" },
  { event: "license_revoked", user: "usr_5512", ip: "admin", time: "2026-04-06 18:45" },
  { event: "login_failed", user: "unknown", ip: "203.0.113.x", time: "2026-04-06 14:30" },
  { event: "preset_pack_decrypted", user: "usr_7744", ip: "172.16.0.x", time: "2026-04-06 12:00" },
];

const SecurityPage = () => (
  <div className="space-y-6">
    <div>
      <h2 className="text-2xl font-bold">Security & Audit</h2>
      <p className="text-muted-foreground text-sm mt-1">License events and security audit trail</p>
    </div>

    <div className="glass-panel overflow-hidden">
      <div className="flex items-center gap-2 p-4 border-b border-border/50">
        <Shield className="w-4 h-4 text-primary" />
        <h3 className="font-semibold text-sm">Audit Log</h3>
      </div>
      <table className="w-full">
        <thead>
          <tr className="border-b border-border/50">
            {["Event", "User", "IP", "Timestamp"].map((h) => (
              <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {auditLog.map((e, i) => (
            <tr key={i} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
              <td className="px-4 py-3 text-sm font-mono">{e.event}</td>
              <td className="px-4 py-3 text-sm font-mono text-highlight-silver">{e.user}</td>
              <td className="px-4 py-3 text-sm text-muted-foreground">{e.ip}</td>
              <td className="px-4 py-3 text-sm text-muted-foreground flex items-center gap-1">
                <Clock className="w-3 h-3" /> {e.time}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  </div>
);

export default SecurityPage;
