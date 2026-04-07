import { Bell, Plus } from "lucide-react";

const announcements = [
  { title: "v1.0.0 Launch", date: "2026-04-01", status: "published", body: "DIDITAGAIN STUDIO is now available..." },
  { title: "Maintenance Window", date: "2026-04-05", status: "draft", body: "Scheduled maintenance on April 10th..." },
];

const AnnouncementsPage = () => (
  <div className="space-y-6">
    <div className="flex items-center justify-between">
      <div>
        <h2 className="text-2xl font-bold">Announcements</h2>
        <p className="text-muted-foreground text-sm mt-1">Push changelogs and announcements to users</p>
      </div>
      <button className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 transition-colors">
        <Plus className="w-4 h-4" /> New Announcement
      </button>
    </div>

    <div className="space-y-4">
      {announcements.map((a) => (
        <div key={a.title} className="glass-panel p-5">
          <div className="flex items-start justify-between">
            <div className="flex items-center gap-3">
              <Bell className="w-5 h-5 text-primary" />
              <div>
                <h3 className="font-semibold">{a.title}</h3>
                <p className="text-sm text-muted-foreground mt-1">{a.body}</p>
              </div>
            </div>
            <div className="text-right">
              <span className={`px-2 py-1 rounded-md text-xs font-medium ${a.status === "published" ? "bg-secondary/20 text-secondary" : "bg-muted text-muted-foreground"}`}>
                {a.status}
              </span>
              <p className="text-xs text-text-dim mt-1">{a.date}</p>
            </div>
          </div>
        </div>
      ))}
    </div>
  </div>
);

export default AnnouncementsPage;
