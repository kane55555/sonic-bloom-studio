import { Users, CreditCard, Monitor, Music, Activity, TrendingUp } from "lucide-react";
import StatCard from "@/components/admin/StatCard";

const recentActivity = [
  { action: "New subscription", user: "user_3829", time: "2 min ago", type: "success" },
  { action: "Device activated", user: "user_1204", time: "15 min ago", type: "info" },
  { action: "Preset pack uploaded", user: "admin", time: "1 hr ago", type: "info" },
  { action: "Subscription expired", user: "user_0891", time: "3 hr ago", type: "warning" },
  { action: "License revoked", user: "user_5512", time: "5 hr ago", type: "destructive" },
];

const Dashboard = () => (
  <div className="space-y-8">
    <div>
      <h2 className="text-2xl font-bold">Dashboard</h2>
      <p className="text-muted-foreground text-sm mt-1">DIDITAGAIN STUDIO overview</p>
    </div>

    <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
      <StatCard icon={Users} label="Total Users" value="1,247" change="+23 this week" accent="purple" />
      <StatCard icon={CreditCard} label="Active Subs" value="834" change="+12 this week" accent="teal" />
      <StatCard icon={Monitor} label="Activations" value="2,091" accent="purple" />
      <StatCard icon={Music} label="Preset Packs" value="8" accent="teal" />
    </div>

    <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
      <div className="glass-panel p-6">
        <div className="flex items-center gap-2 mb-4">
          <Activity className="w-4 h-4 text-primary" />
          <h3 className="font-semibold">Recent Activity</h3>
        </div>
        <div className="space-y-3">
          {recentActivity.map((item, i) => (
            <div key={i} className="flex items-center justify-between py-2 border-b border-border/50 last:border-0">
              <div>
                <p className="text-sm font-medium">{item.action}</p>
                <p className="text-xs text-muted-foreground font-mono">{item.user}</p>
              </div>
              <span className="text-xs text-text-dim">{item.time}</span>
            </div>
          ))}
        </div>
      </div>

      <div className="glass-panel p-6">
        <div className="flex items-center gap-2 mb-4">
          <TrendingUp className="w-4 h-4 text-secondary" />
          <h3 className="font-semibold">Quick Actions</h3>
        </div>
        <div className="grid grid-cols-2 gap-3">
          {["Upload Preset Pack", "Push Announcement", "View Audit Log", "Export Users"].map((action) => (
            <button
              key={action}
              className="p-3 rounded-lg bg-muted hover:bg-muted/80 text-sm font-medium text-foreground transition-colors text-left"
            >
              {action}
            </button>
          ))}
        </div>
      </div>
    </div>
  </div>
);

export default Dashboard;
