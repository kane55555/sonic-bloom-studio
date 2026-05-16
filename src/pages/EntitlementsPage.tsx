import { useState } from "react";
import { toast } from "sonner";
import { Gift, Trash2, Plus } from "lucide-react";
import {
  useEntitlements, useGrantPack, useRevokePack, useUsers, usePacks,
} from "@/lib/admin/hooks";
import AsyncBoundary from "@/components/admin/AsyncBoundary";
import { grantPackInputSchema, type GrantPackInput } from "@/lib/admin/schemas";

const EntitlementsPage = () => {
  const { data, isLoading, error } = useEntitlements();
  const { data: users } = useUsers();
  const { data: packs } = usePacks();
  const grant = useGrantPack();
  const revoke = useRevokePack();
  const [form, setForm] = useState<Partial<GrantPackInput>>({ source: "grant" });

  const onGrant = (e: React.FormEvent) => {
    e.preventDefault();
    const parsed = grantPackInputSchema.safeParse(form);
    if (!parsed.success) { toast.error(parsed.error.issues[0]?.message ?? "Invalid"); return; }
    grant.mutate(parsed.data, {
      onSuccess: () => { toast.success("Pack granted"); setForm({ source: "grant" }); },
      onError: (e) => toast.error(e instanceof Error ? e.message : "Failed"),
    });
  };

  const rows = data ?? [];

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold">Entitlements</h2>
        <p className="text-muted-foreground text-sm mt-1">Per-user pack access (user_packs)</p>
      </div>

      <form onSubmit={onGrant} className="glass-panel p-5 grid grid-cols-4 gap-3">
        <select value={form.user_id ?? ""}
          onChange={(e) => setForm({ ...form, user_id: e.target.value })}
          className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
          <option value="">Select user…</option>
          {(users ?? []).map((u) => <option key={u.id} value={u.id}>{u.email}</option>)}
        </select>
        <select value={form.pack_id ?? ""}
          onChange={(e) => setForm({ ...form, pack_id: e.target.value })}
          className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
          <option value="">Select pack…</option>
          {(packs ?? []).map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
        </select>
        <select value={form.source ?? "grant"}
          onChange={(e) => setForm({ ...form, source: e.target.value as GrantPackInput["source"] })}
          className="px-3 py-2 rounded-md bg-muted border border-border text-sm">
          <option value="grant">grant</option>
          <option value="purchase">purchase</option>
          <option value="bundle">bundle</option>
        </select>
        <button type="submit" disabled={grant.isPending}
          className="px-4 py-2 rounded-md bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 disabled:opacity-50">
          <Plus className="w-4 h-4 inline mr-1" /> Grant
        </button>
      </form>

      <AsyncBoundary
        isLoading={isLoading} error={error}
        isEmpty={!isLoading && !error && rows.length === 0}
        emptyMessage="No pack entitlements yet.">
        <div className="glass-panel overflow-hidden">
          <table className="w-full">
            <thead>
              <tr className="border-b border-border/50">
                {["User", "Pack", "Source", "Granted", ""].map((h) => (
                  <th key={h} className="px-4 py-3 text-left text-xs font-semibold text-muted-foreground uppercase tracking-wider">{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((r) => (
                <tr key={`${r.user_id}-${r.pack_id}`} className="border-b border-border/30 hover:bg-muted/30 transition-colors">
                  <td className="px-4 py-3 text-sm">{r.user_email ?? r.user_id.slice(0, 8)}</td>
                  <td className="px-4 py-3 text-sm flex items-center gap-2">
                    <Gift className="w-4 h-4 text-primary" /> {r.pack_name}
                  </td>
                  <td className="px-4 py-3 text-xs capitalize">{r.source}</td>
                  <td className="px-4 py-3 text-sm text-muted-foreground">{r.granted_at.slice(0, 10)}</td>
                  <td className="px-4 py-3">
                    <button onClick={() => revoke.mutate({ userId: r.user_id, packId: r.pack_id }, {
                      onSuccess: () => toast.success("Revoked"),
                    })} className="text-destructive hover:text-destructive/80">
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

export default EntitlementsPage;
