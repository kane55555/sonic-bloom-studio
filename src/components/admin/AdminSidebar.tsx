import { NavLink } from "react-router-dom";
import { Users, CreditCard, Music, Shield, Monitor, Bell, LayoutDashboard, Settings, Scissors, FileAudio } from "lucide-react";

const navItems = [
  { to: "/", icon: LayoutDashboard, label: "Dashboard" },
  { to: "/users", icon: Users, label: "Users" },
  { to: "/subscriptions", icon: CreditCard, label: "Subscriptions" },
  { to: "/presets", icon: Music, label: "Preset Packs" },
  { to: "/factory-samples", icon: FileAudio, label: "Factory One-Shots" },
  { to: "/samples", icon: Scissors, label: "User Samples" },
  { to: "/activations", icon: Monitor, label: "Activations" },
  { to: "/security", icon: Shield, label: "Security" },
  { to: "/announcements", icon: Bell, label: "Announcements" },
  { to: "/settings", icon: Settings, label: "Settings" },
];

const AdminSidebar = () => (
  <aside className="w-64 min-h-screen bg-sidebar border-r border-sidebar-border flex flex-col">
    <div className="p-6 border-b border-sidebar-border">
      <h1 className="text-xl font-bold gradient-text tracking-tight">DIDITAGAIN</h1>
      <p className="text-xs text-muted-foreground mt-1 font-mono">STUDIO ADMIN</p>
    </div>
    <nav className="flex-1 p-3 space-y-1">
      {navItems.map(({ to, icon: Icon, label }) => (
        <NavLink
          key={to}
          to={to}
          end={to === "/"}
          className={({ isActive }) =>
            `flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm font-medium transition-all ${
              isActive
                ? "bg-primary/15 text-primary glow-purple"
                : "text-sidebar-foreground hover:bg-sidebar-accent hover:text-sidebar-accent-foreground"
            }`
          }
        >
          <Icon className="w-4 h-4" />
          {label}
        </NavLink>
      ))}
    </nav>
    <div className="p-4 border-t border-sidebar-border">
      <div className="glass-panel p-3 text-center">
        <p className="text-xs text-muted-foreground">v1.0.0-alpha</p>
      </div>
    </div>
  </aside>
);

export default AdminSidebar;
