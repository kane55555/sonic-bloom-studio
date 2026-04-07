import { Settings, Globe, Key, Database } from "lucide-react";

const SettingsPage = () => (
  <div className="space-y-6">
    <div>
      <h2 className="text-2xl font-bold">Settings</h2>
      <p className="text-muted-foreground text-sm mt-1">System configuration</p>
    </div>

    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
      {[
        { icon: Globe, title: "API Configuration", desc: "Backend URL, webhook endpoints" },
        { icon: Key, title: "License Settings", desc: "Max devices, grace period, token TTL" },
        { icon: Database, title: "Database", desc: "Supabase connection and migration status" },
        { icon: Settings, title: "General", desc: "App name, version, branding" },
      ].map(({ icon: Icon, title, desc }) => (
        <div key={title} className="glass-panel p-5 flex items-start gap-4 cursor-pointer hover:border-primary/30 transition-colors">
          <div className="p-2.5 rounded-lg bg-muted">
            <Icon className="w-5 h-5 text-highlight-silver" />
          </div>
          <div>
            <h3 className="font-semibold">{title}</h3>
            <p className="text-sm text-muted-foreground mt-1">{desc}</p>
          </div>
        </div>
      ))}
    </div>
  </div>
);

export default SettingsPage;
