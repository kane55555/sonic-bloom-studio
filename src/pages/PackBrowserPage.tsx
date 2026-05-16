import { Package, Plus, Trash2, Eye, EyeOff } from "lucide-react";
import { useState } from "react";
import { toast } from "sonner";
import {
  usePacks, useCreatePack, useDeletePack, useSetPackPublished,
} from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";
import { packInputSchema, type PackInput } from "@/lib/admin/schemas";

const PackBrowserPage = () => {
  const { data, isLoading, error } = usePacks();
  const create = useCreatePack();
  const del = useDeletePack();
  const setPub = useSetPackPublished();
  const [open, setOpen] = useState(false);
  const [form, setForm] = useState<Partial<PackInput>>({
    slug: "", name: "", instrument_type: "sampler",
    required_plan: "free", version: "1.0.0", is_factory: false, is_published: false,
  });

  const onSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const parsed = packInputSchema.safeParse(form);
    if (!parsed.success) { toast.error(parsed.error.issues[0]?.message ?? "Invalid"); return; }
    create.mutate(parsed.data, {
      onSuccess: () => { toast.success("Pack created"); setOpen(false); },
      onError: (e) => toast.error(e instanceof Error ? e.message : "Failed"),
    });
  };

  const rows = data ?? [];

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-bold">Preset Packs</h2>
          <p className="text-muted-foreground text-sm mt-1">Manage downloadable preset packs and publishing state</p>
        </div>
        <button onClick={() => setOpen((o) => !o)}
          className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90">
          <Plus className="w-4 h-4" /> {open ? "Cancel" : "New Pack"}
        </button>
      </div>

      {open && (
        <form onSubmit={onSubmit} className="glass-panel p-5 grid grid-cols-2 gap-3">
          <input placeholder="slug (lowercase-dashes)" value={form.slug ?? ""}
            onChange={(e) => setForm({ ...form, slug: e.target.value })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <input placeholder="Name" value={form.name ?? ""}
            onChange={(e) => setForm({ ...form, name: e.target.value })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <input placeholder="Instrument type" value={form.instrument_type ?? ""}
            onChange={(e) => setForm({ ...form, instrument_type: e.target.value })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <input placeholder="Version (1.0.0)" value={form.version ?? ""}
            onChange={(e) => setForm({ ...form, version: e.target.value })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <select value={form.required_plan ?? "free"}
            onChange={(e) => setForm({ ...form, required_plan: e.target.value as PackInput["required_plan"] })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
            <option value="free">free</option>
            <option value="basic">basic</option>
            <option value="pro">pro</option>
          </select>
          <label className="flex items-center gap-2 text-sm">
            <input type="checkbox" checked={form.is_factory ?? false}
              onChange={(e) => setForm({ ...form, is_factory: e.target.checked })} /> factory
          </label>
          <textarea placeholder="Description" value={form.description ?? ""}
            onChange={(e) => setForm({ ...form, description: e.target.value })}
            className="col-span-2 px-3 py-2 rounded-md bg-muted border border-border text-sm" rows={3} />
          <button type="submit" disabled={create.isPending}
            className="col-span-2 px-4 py-2 rounded-md bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 disabled:opacity-50">
            {create.isPending ? "Saving…" : "Create pack"}
          </button>
        </form>
      )}

      <AsyncBoundary
        isLoading={isLoading} error={error}
        isEmpty={!isLoading && !error && rows.length === 0}
        emptyMessage="No preset packs yet.">
        <div className="glass-panel overflow-hidden">
          <table className="w-full">
            <thead>
              <tr className="border-b border-border/50">
                {["Pack", "Slug", "Plan", "Version", "Presets", "Published", ""].map((h) => (
                  <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((p) => (
                <tr key={p.id} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
                  <td className="px-4 py-3 text-sm font-medium flex items-center gap-2">
                    <Package className="w-4 h-4 text-primary" /> {p.name}
                  </td>
                  <td className="px-4 py-3 text-xs font-mono text-highlight-silver">{p.slug}</td>
                  <td className="px-4 py-3 text-sm capitalize">{p.required_plan}</td>
                  <td className="px-4 py-3 text-xs font-mono">{p.version}</td>
                  <td className="px-4 py-3 text-sm">{p.preset_count}</td>
                  <td className="px-4 py-3">
                    <button
                      onClick={() => setPub.mutate({ id: p.id, published: !p.is_published }, {
                        onSuccess: () => toast.success(p.is_published ? "Unpublished" : "Published"),
                      })}
                      className={`flex items-center gap-1 text-xs ${p.is_published ? "text-secondary" : "text-muted-foreground"}`}>
                      {p.is_published ? <Eye className="w-3 h-3" /> : <EyeOff className="w-3 h-3" />}
                      {p.is_published ? "published" : "draft"}
                    </button>
                  </td>
                  <td className="px-4 py-3">
                    <button onClick={() => del.mutate(p.id, { onSuccess: () => toast.success("Deleted") })}
                      className="text-destructive hover:text-destructive/80">
                      <Trash2 className="w-4 h-4" />
                    </button>
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

export default PackBrowserPage;
