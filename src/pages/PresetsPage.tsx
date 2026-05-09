import { useMemo, useState } from "react";
import { Music, Upload, Star, Search, AlertCircle, Filter, Layers } from "lucide-react";
import { ALL_CATEGORIES, type PresetCategory } from "@diditagain/preset-schema";

type IndexEntry = {
  presetId: string;
  name: string;
  bank: "Factory" | "User";
  category: PresetCategory;
  tags: string[];
  presetPath: string;
  samplePath: string;
  needsReview: boolean;
  favorite: boolean;
};

// Demo seed — in production this is loaded from
// Documents/DIDITAGAIN STUDIO/Presets/index.json (or the backend equivalent).
const DEMO_INDEX: IndexEntry[] = [
  { presetId: "1", name: "Dark Bell C5", bank: "User", category: "DrillBells",
    tags: ["drill","bell","dark","imported"], presetPath: "Presets/User/DrillBells/Dark_Bell_C5.didasynthpreset",
    samplePath: "Samples/Imported/DrillBells/Dark_Bell_C5.wav", needsReview: false, favorite: true },
  { presetId: "2", name: "Heavy 808 C2", bank: "User", category: "Bass808",
    tags: ["808","sub","drill"], presetPath: "Presets/User/Bass808/Heavy_808.didasynthpreset",
    samplePath: "Samples/Imported/Bass808/Heavy_808.wav", needsReview: true, favorite: false },
  { presetId: "3", name: "Sad Piano F3", bank: "User", category: "PainPianos",
    tags: ["piano","emotional"], presetPath: "Presets/User/PainPianos/Sad_Piano_F3.didasynthpreset",
    samplePath: "Samples/Imported/PainPianos/Sad_Piano_F3.wav", needsReview: false, favorite: false },
  { presetId: "4", name: "Big Riser", bank: "User", category: "FXRisers",
    tags: ["fx","riser","build"], presetPath: "Presets/User/FXRisers/Big_Riser.didasynthpreset",
    samplePath: "Samples/Imported/FXRisers/Big_Riser.wav", needsReview: true, favorite: false },
  { presetId: "5", name: "Sampled Brass", bank: "Factory", category: "AlienLeads",
    tags: ["brass","factory"], presetPath: "Presets/Factory/Sampled_Brass.didasynthpreset",
    samplePath: "", needsReview: false, favorite: true },
];

const PresetsPage = () => {
  const [bankTab, setBankTab] = useState<"All" | "Factory" | "User">("All");
  const [category, setCategory] = useState<PresetCategory | "All">("All");
  const [needsReviewOnly, setNeedsReviewOnly] = useState(false);
  const [query, setQuery] = useState("");
  const [selected, setSelected] = useState<IndexEntry | null>(null);

  const filtered = useMemo(() => DEMO_INDEX.filter(p =>
    (bankTab === "All" || p.bank === bankTab) &&
    (category === "All" || p.category === category) &&
    (!needsReviewOnly || p.needsReview) &&
    (query.trim() === "" ||
      p.name.toLowerCase().includes(query.toLowerCase()) ||
      p.tags.some(t => t.toLowerCase().includes(query.toLowerCase())))
  ), [bankTab, category, needsReviewOnly, query]);

  const stats = useMemo(() => {
    const byCat: Record<string, number> = {};
    DEMO_INDEX.forEach(p => { byCat[p.category] = (byCat[p.category] ?? 0) + 1; });
    return {
      total: DEMO_INDEX.length,
      factory: DEMO_INDEX.filter(p => p.bank === "Factory").length,
      user: DEMO_INDEX.filter(p => p.bank === "User").length,
      needsReview: DEMO_INDEX.filter(p => p.needsReview).length,
      byCat,
    };
  }, []);

  return (
    <div className="space-y-6">
      <header className="flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-bold">Preset Library</h2>
          <p className="text-muted-foreground text-sm mt-1">
            Hybrid v2 presets — sampler + 4-layer editable engine
          </p>
        </div>
        <button className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 transition-colors">
          <Upload className="w-4 h-4" /> Import Samples
        </button>
      </header>

      {/* Overview cards */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
        <StatCard label="Total Presets" value={stats.total} icon={<Music className="w-4 h-4" />} />
        <StatCard label="Factory" value={stats.factory} icon={<Layers className="w-4 h-4" />} />
        <StatCard label="User" value={stats.user} icon={<Star className="w-4 h-4" />} />
        <StatCard label="Needs Review" value={stats.needsReview} icon={<AlertCircle className="w-4 h-4 text-destructive" />} />
      </div>

      {/* Bank tabs */}
      <div className="flex items-center gap-1 border-b border-border">
        {(["All","Factory","User"] as const).map(t => (
          <button key={t} onClick={() => setBankTab(t)}
            className={`px-4 py-2 text-sm font-medium border-b-2 transition-colors ${
              bankTab === t ? "border-primary text-foreground" : "border-transparent text-muted-foreground hover:text-foreground"
            }`}>{t}</button>
        ))}
      </div>

      {/* Filters */}
      <div className="flex flex-wrap items-center gap-3">
        <div className="relative flex-1 min-w-[220px]">
          <Search className="w-4 h-4 absolute left-3 top-1/2 -translate-y-1/2 text-muted-foreground" />
          <input value={query} onChange={(e) => setQuery(e.target.value)}
            placeholder="Search presets, tags…"
            className="w-full pl-10 pr-3 py-2 rounded-lg bg-card border border-border text-sm" />
        </div>
        <select value={category} onChange={(e) => setCategory(e.target.value as PresetCategory | "All")}
          className="px-3 py-2 rounded-lg bg-card border border-border text-sm">
          <option value="All">All Categories</option>
          {ALL_CATEGORIES.map(c => <option key={c} value={c}>{c}</option>)}
        </select>
        <button onClick={() => setNeedsReviewOnly(v => !v)}
          className={`px-3 py-2 rounded-lg border text-sm flex items-center gap-2 ${
            needsReviewOnly ? "bg-destructive/10 border-destructive text-destructive" : "bg-card border-border text-muted-foreground"
          }`}>
          <Filter className="w-4 h-4" /> Needs Review
        </button>
      </div>

      {/* Grid + detail */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
        <div className="lg:col-span-2 grid grid-cols-1 md:grid-cols-2 gap-3">
          {filtered.length === 0 && (
            <div className="col-span-full glass-panel p-8 text-center text-sm text-muted-foreground">
              No presets match your filters.
            </div>
          )}
          {filtered.map(p => (
            <button key={p.presetId} onClick={() => setSelected(p)}
              className={`text-left glass-panel p-4 hover:border-primary/50 transition-colors ${
                selected?.presetId === p.presetId ? "border-primary" : ""
              }`}>
              <div className="flex items-start justify-between gap-2">
                <div className="min-w-0">
                  <h3 className="font-semibold truncate">{p.name}</h3>
                  <p className="text-xs text-muted-foreground mt-0.5">
                    {p.category} • {p.bank}
                  </p>
                </div>
                {p.needsReview && (
                  <span className="px-2 py-0.5 text-[10px] font-bold uppercase rounded bg-destructive/15 text-destructive">Review</span>
                )}
              </div>
              <div className="flex flex-wrap gap-1 mt-3">
                {p.tags.slice(0, 4).map(t => (
                  <span key={t} className="px-1.5 py-0.5 text-[10px] rounded bg-muted text-muted-foreground">{t}</span>
                ))}
              </div>
            </button>
          ))}
        </div>

        {/* Detail panel */}
        <aside className="glass-panel p-5 h-fit sticky top-4">
          {selected ? (
            <>
              <div className="flex items-start justify-between">
                <div>
                  <h3 className="font-bold text-lg">{selected.name}</h3>
                  <p className="text-xs text-muted-foreground mt-1">{selected.category} • {selected.bank}</p>
                </div>
                <Star className={`w-4 h-4 ${selected.favorite ? "fill-primary text-primary" : "text-muted-foreground"}`} />
              </div>
              <dl className="mt-4 space-y-2 text-xs">
                <Row k="Sample" v={selected.samplePath || "—"} />
                <Row k="Preset" v={selected.presetPath} />
                <Row k="Tags"   v={selected.tags.join(", ")} />
                <Row k="Status" v={selected.needsReview ? "Needs Review" : "OK"} />
              </dl>
              <button className="mt-5 w-full px-3 py-2 rounded-lg bg-primary text-primary-foreground text-sm font-medium">
                Edit Layers
              </button>
            </>
          ) : (
            <p className="text-sm text-muted-foreground">Select a preset to inspect.</p>
          )}
        </aside>
      </div>
    </div>
  );
};

const StatCard = ({ label, value, icon }: { label: string; value: number; icon: React.ReactNode }) => (
  <div className="glass-panel p-4">
    <div className="flex items-center justify-between text-muted-foreground">
      <span className="text-xs uppercase tracking-wide">{label}</span>
      {icon}
    </div>
    <p className="text-2xl font-bold mt-2">{value}</p>
  </div>
);

const Row = ({ k, v }: { k: string; v: string }) => (
  <div className="flex justify-between gap-3">
    <dt className="text-muted-foreground shrink-0">{k}</dt>
    <dd className="text-right text-foreground truncate">{v}</dd>
  </div>
);

export default PresetsPage;
