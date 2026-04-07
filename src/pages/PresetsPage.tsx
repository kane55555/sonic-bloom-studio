import { Music, Upload, Star, Package } from "lucide-react";

const packs = [
  { name: "Factory Essentials", presets: 30, tier: "Free", status: "Published" },
  { name: "Dark Drill Kit", presets: 15, tier: "Pro", status: "Draft" },
  { name: "Ambient Textures", presets: 20, tier: "Pro", status: "Published" },
  { name: "Trap Brass Collection", presets: 12, tier: "Pro", status: "Published" },
];

const PresetsPage = () => (
  <div className="space-y-6">
    <div className="flex items-center justify-between">
      <div>
        <h2 className="text-2xl font-bold">Preset Packs</h2>
        <p className="text-muted-foreground text-sm mt-1">Manage official and premium preset packs</p>
      </div>
      <button className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-primary text-primary-foreground text-sm font-medium hover:bg-primary/90 transition-colors">
        <Upload className="w-4 h-4" /> Upload Pack
      </button>
    </div>

    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
      {packs.map((p) => (
        <div key={p.name} className="glass-panel p-5 flex items-start gap-4">
          <div className="p-3 rounded-lg bg-primary/10">
            <Package className="w-6 h-6 text-primary" />
          </div>
          <div className="flex-1">
            <div className="flex items-center gap-2">
              <h3 className="font-semibold">{p.name}</h3>
              {p.tier === "Pro" && (
                <span className="px-2 py-0.5 rounded text-[10px] font-bold bg-primary/20 text-primary uppercase">Pro</span>
              )}
            </div>
            <p className="text-sm text-muted-foreground mt-1">{p.presets} presets • {p.status}</p>
          </div>
          <button className="text-muted-foreground hover:text-foreground">
            <Star className="w-4 h-4" />
          </button>
        </div>
      ))}
    </div>
  </div>
);

export default PresetsPage;
