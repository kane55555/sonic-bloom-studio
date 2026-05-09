import { useMemo, useState } from "react";
import { Trash2, Edit2, Star, RefreshCw } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { ALL_CATEGORIES, type PresetCategory } from "../../../packages/preset-schema/src";

export type AdminPreset = {
  id: string;
  name: string;
  category: PresetCategory;
  bank: "Factory" | "User";
  tags: string[];
  favorite: boolean;
};

interface Props {
  presets: AdminPreset[];
  onEdit?: (p: AdminPreset) => void;
  onDelete?: (id: string) => void;
  onToggleFavorite?: (id: string) => void;
  onReindex?: () => void;
}

export const PresetBrowserAdmin = ({ presets, onEdit, onDelete, onToggleFavorite, onReindex }: Props) => {
  const [query, setQuery] = useState("");
  const [cat, setCat] = useState<PresetCategory | "All">("All");

  const filtered = useMemo(() =>
    presets.filter(p =>
      (cat === "All" || p.category === cat) &&
      (query === "" || p.name.toLowerCase().includes(query.toLowerCase()))
    ), [presets, query, cat]);

  return (
    <Card className="p-6 space-y-4 bg-card border-border">
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold">Preset Browser (Admin)</h3>
        <Button variant="outline" size="sm" onClick={onReindex}>
          <RefreshCw className="w-4 h-4 mr-2" />Re-index
        </Button>
      </div>

      <div className="flex gap-2 flex-wrap">
        <Input placeholder="Search…" value={query} onChange={e => setQuery(e.target.value)} className="flex-1 min-w-48" />
        <div className="flex gap-1 flex-wrap">
          <Badge
            variant={cat === "All" ? "default" : "outline"}
            className="cursor-pointer"
            onClick={() => setCat("All")}
          >All</Badge>
          {ALL_CATEGORIES.map(c => (
            <Badge
              key={c}
              variant={cat === c ? "default" : "outline"}
              className="cursor-pointer"
              onClick={() => setCat(c)}
            >{c}</Badge>
          ))}
        </div>
      </div>

      <div className="space-y-1 max-h-96 overflow-y-auto">
        {filtered.map(p => (
          <div key={p.id} className="flex items-center gap-2 p-2 rounded-md border border-border bg-background">
            <Star
              className={`w-4 h-4 cursor-pointer ${p.favorite ? "text-primary fill-primary" : "text-muted-foreground"}`}
              onClick={() => onToggleFavorite?.(p.id)}
            />
            <span className="flex-1 truncate">{p.name}</span>
            <Badge variant="secondary">{p.category}</Badge>
            <Badge variant={p.bank === "Factory" ? "default" : "outline"}>{p.bank}</Badge>
            <Button size="sm" variant="ghost" onClick={() => onEdit?.(p)}><Edit2 className="w-4 h-4" /></Button>
            <Button size="sm" variant="ghost" onClick={() => onDelete?.(p.id)}><Trash2 className="w-4 h-4" /></Button>
          </div>
        ))}
        {filtered.length === 0 && (
          <div className="text-center text-muted-foreground text-sm py-8">No presets match.</div>
        )}
      </div>
    </Card>
  );
};

export default PresetBrowserAdmin;
