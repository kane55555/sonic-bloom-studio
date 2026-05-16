import { useState } from "react";
import { toast } from "sonner";
import { GitBranch, Plus, Trash2, Star } from "lucide-react";
import {
  usePacks, usePackVersions, useCreatePackVersion, useDeletePackVersion,
} from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";
import { packVersionInputSchema, type PackVersionInput } from "@/lib/admin/schemas";

const PackVersionsPage = () => {
  const { data: packs } = usePacks();
  const [packId, setPackId] = useState<string>("");
  const { data, isLoading, error } = usePackVersions(packId || undefined);
  const create = useCreatePackVersion();
  const del = useDeletePackVersion();

  const [form, setForm] = useState<Partial<PackVersionInput>>({
    is_latest: true, version: "1.0.0",
  });

  const onSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const parsed = packVersionInputSchema.safeParse({ ...form, pack_id: packId });
    if (!parsed.success) { toast.error(parsed.error.issues[0]?.message ?? "Invalid"); return; }
    create.mutate(parsed.data, {
      onSuccess: () => { toast.success("Version created"); setForm({ is_latest: true, version: "1.0.0" }); },
      onError: (e) => toast.error(e instanceof Error ? e.message : "Failed"),
    });
  };

  const rows = data ?? [];

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold">Pack Versions</h2>
        <p className="text-muted-foreground text-sm mt-1">Manage manifest + download URLs per pack version</p>
      </div>

      <div className="flex items-center gap-3">
        <select value={packId} onChange={(e) => setPackId(e.target.value)}
          className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
          <option value="">All packs</option>
          {(packs ?? []).map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
        </select>
      </div>

      {packId && (
        <form onSubmit={onSubmit} className="glass-panel p-5 grid grid-cols-2 gap-3">
          <input placeholder="Version (1.0.0)" value={form.version ?? ""}
            onChange={(e) => setForm({ ...form, version: e.target.value })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <input placeholder="checksum_sha256 (64 hex chars)" value={form.checksum_sha256 ?? ""}
            onChange={(e) => setForm({ ...form, checksum_sha256: e.target.value })}
            className="px-3 py-2 rounded-md bg-muted border border-border text-sm font-mono text-xs" />
          <input placeholder="manifest_url (https://…)" value={form.manifest_url ?? ""}
            onChange={(e) => setForm({ ...form, manifest_url: e.target.value })}
            className="col-span-2 px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <input placeholder="download_url (optional, signed)" value={form.download_url ?? ""}
            onChange={(e) => setForm({ ...form, download_url: e.target.value })}
            className="col-span-2 px-3 py-2 rounded-md bg-muted border border-border text-sm" />
          <textarea placeholder="Changelog" value={form.changelog ?? ""}
            onChange={(e) => setForm({ ...form, changelog: e.target.value })}
            className="col-span-2 px-3 py-2 rounded-md bg-muted border border-border text-sm" rows={2} />
          <label className="flex items-center gap-2 text-sm col-span-2">
            <input type="checkbox" checked={form.is_latest ?? true}
              onChange={(e) => setForm({ ...form, is_latest: e.target.checked })} />
            mark as latest
          </label>
          <button type="submit" disabled={create.isPending}
            className="col-span-2 px-4 py-2 rounded-md bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 disabled:opacity-50">
            <Plus className="w-4 h-4 inline mr-1" />
            {create.isPending ? "Saving…" : "Add version"}
          </button>
        </form>
      )}

      <AsyncBoundary
        isLoading={isLoading} error={error}
        isEmpty={!isLoading && !error && rows.length === 0}
        emptyMessage={packId ? "No versions for this pack yet." : "Select a pack to view versions."}>
        <div className="glass-panel overflow-hidden">
          <table className="w-full">
            <thead>
              <tr className="border-b border-border/50">
                {["Version", "Manifest", "Download", "Checksum", "Latest", "Created", ""].map((h) => (
                  <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((v) => (
                <tr key={v.id} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
                  <td className="px-4 py-3 text-sm font-mono flex items-center gap-2">
                    <GitBranch className="w-3 h-3" /> {v.version}
                  </td>
                  <td className="px-4 py-3 text-xs truncate max-w-[200px]">
                    <a href={v.manifest_url} target="_blank" rel="noreferrer" className="text-primary hover:underline">link</a>
                  </td>
                  <td className="px-4 py-3 text-xs">
                    {v.download_url ? (
                      <a href={v.download_url} target="_blank" rel="noreferrer" className="text-primary hover:underline">link</a>
                    ) : <span className="text-muted-foreground">—</span>}
                  </td>
                  <td className="px-4 py-3 text-xs font-mono text-muted-foreground" title={v.checksum_sha256}>
                    {v.checksum_sha256.slice(0, 10)}…
                  </td>
                  <td className="px-4 py-3">
                    {v.is_latest && <Star className="w-4 h-4 text-secondary fill-secondary" />}
                  </td>
                  <td className="px-4 py-3 text-sm text-muted-foreground">{v.created_at.slice(0, 10)}</td>
                  <td className="px-4 py-3">
                    <button onClick={() => del.mutate(v.id, { onSuccess: () => toast.success("Deleted") })}
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

export default PackVersionsPage;
