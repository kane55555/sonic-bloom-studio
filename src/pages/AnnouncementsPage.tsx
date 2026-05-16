import { Bell, Plus, Trash2 } from "lucide-react";
import { useState } from "react";
import { toast } from "sonner";
import {
  useAnnouncements, useCreateAnnouncement, useDeleteAnnouncement,
} from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";
import { announcementInputSchema } from "@/lib/admin/schemas";

const AnnouncementsPage = () => {
  const { data, isLoading, error } = useAnnouncements();
  const create = useCreateAnnouncement();
  const remove = useDeleteAnnouncement();
  const [open, setOpen] = useState(false);
  const [title, setTitle] = useState("");
  const [body, setBody] = useState("");
  const [status, setStatus] = useState<"draft" | "published">("draft");

  const onSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const parsed = announcementInputSchema.safeParse({ title, body, status });
    if (!parsed.success) {
      toast.error(parsed.error.issues[0]?.message ?? "Invalid input");
      return;
    }
    create.mutate(parsed.data, {
      onSuccess: () => {
        toast.success("Announcement created");
        setOpen(false); setTitle(""); setBody(""); setStatus("draft");
      },
      onError: (e) => toast.error(e instanceof Error ? e.message : "Failed"),
    });
  };

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-bold">Announcements</h2>
          <p className="text-muted-foreground text-sm mt-1">Push changelogs and announcements to users</p>
        </div>
        <button
          onClick={() => setOpen((o) => !o)}
          className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 transition-colors"
        >
          <Plus className="w-4 h-4" /> {open ? "Cancel" : "New Announcement"}
        </button>
      </div>

      {open && (
        <form onSubmit={onSubmit} className="glass-panel p-5 space-y-3">
          <input
            value={title} onChange={(e) => setTitle(e.target.value)} maxLength={200}
            placeholder="Title" className="w-full px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <textarea
            value={body} onChange={(e) => setBody(e.target.value)} maxLength={5000} rows={4}
            placeholder="Body" className="w-full px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <div className="flex items-center gap-3">
            <select
              value={status} onChange={(e) => setStatus(e.target.value as "draft" | "published")}
              className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
              <option value="draft">draft</option>
              <option value="published">published</option>
            </select>
            <button type="submit" disabled={create.isPending}
              className="px-4 py-2 rounded-md bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 disabled:opacity-50">
              {create.isPending ? "Saving…" : "Save"}
            </button>
          </div>
        </form>
      )}

      <AsyncBoundary
        isLoading={isLoading}
        error={error}
        isEmpty={!isLoading && !error && (data ?? []).length === 0}
        emptyMessage="No announcements yet."
      >
        <div className="space-y-4">
          {(data ?? []).map((a) => (
            <div key={a.id} className="glass-panel p-5">
              <div className="flex items-start justify-between gap-4">
                <div className="flex items-start gap-3 flex-1">
                  <Bell className="w-5 h-5 text-primary mt-0.5" />
                  <div className="flex-1">
                    <h3 className="font-semibold">{a.title}</h3>
                    <p className="text-sm text-muted-foreground mt-1 whitespace-pre-wrap">{a.body}</p>
                  </div>
                </div>
                <div className="text-right flex flex-col items-end gap-2">
                  <span className={`px-2 py-1 rounded-md text-xs font-medium ${a.status === "published" ? "bg-secondary/20 text-secondary" : "bg-muted text-muted-foreground"}`}>
                    {a.status}
                  </span>
                  <p className="text-xs text-text-dim">{a.created_at.slice(0, 10)}</p>
                  <button
                    onClick={() => remove.mutate(a.id, { onSuccess: () => toast.success("Deleted") })}
                    className="text-destructive hover:text-destructive/80">
                    <Trash2 className="w-4 h-4" />
                  </button>
                </div>
              </div>
            </div>
          ))}
        </div>
      </AsyncBoundary>
    </div>
  );
};

export default AnnouncementsPage;
