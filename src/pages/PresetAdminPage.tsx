import { useState } from "react";
import PresetImportPanel, { type ImportCandidate } from "@/components/admin/PresetImportPanel";
import PresetBrowserAdmin, { type AdminPreset } from "@/components/admin/PresetBrowserAdmin";
import PresetTemplateEditor, { type CategoryTemplate } from "@/components/admin/PresetTemplateEditor";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { toast } from "sonner";

const SEED_PRESETS: AdminPreset[] = [
  { id: "1", name: "Dark Bell C5", category: "DrillBells", bank: "User", tags: ["drill","bell"], favorite: true },
  { id: "2", name: "Heavy 808",     category: "Bass808",    bank: "User", tags: ["808"],          favorite: false },
  { id: "3", name: "Sad Piano",     category: "PainPianos", bank: "User", tags: ["piano"],        favorite: false },
];

const PresetAdminPage = () => {
  const [presets, setPresets] = useState<AdminPreset[]>(SEED_PRESETS);

  const handleFinalize = (items: ImportCandidate[]) => {
    const added: AdminPreset[] = items.map(i => ({
      id: i.id,
      name: i.filename.replace(/\.[^.]+$/, ""),
      category: i.detectedCategory,
      bank: "User",
      tags: ["imported"],
      favorite: false,
    }));
    setPresets(p => [...p, ...added]);
    toast.success(`Imported ${added.length} preset(s)`);
  };

  const handleSaveTemplate = (t: CategoryTemplate) =>
    toast.success(`Template saved for ${t.category}`);

  return (
    <div className="container mx-auto p-6 space-y-6">
      <h1 className="text-3xl font-bold">Preset Admin</h1>
      <Tabs defaultValue="import">
        <TabsList>
          <TabsTrigger value="import">Import</TabsTrigger>
          <TabsTrigger value="browse">Browse</TabsTrigger>
          <TabsTrigger value="templates">Templates</TabsTrigger>
        </TabsList>
        <TabsContent value="import" className="mt-4">
          <PresetImportPanel onFinalize={handleFinalize} />
        </TabsContent>
        <TabsContent value="browse" className="mt-4">
          <PresetBrowserAdmin
            presets={presets}
            onDelete={(id) => setPresets(p => p.filter(x => x.id !== id))}
            onToggleFavorite={(id) =>
              setPresets(p => p.map(x => x.id === id ? { ...x, favorite: !x.favorite } : x))
            }
            onEdit={(p) => toast.info(`Edit ${p.name} (TODO open editor modal)`)}
            onReindex={() => toast.success("Re-indexed presets")}
          />
        </TabsContent>
        <TabsContent value="templates" className="mt-4">
          <PresetTemplateEditor onSave={handleSaveTemplate} />
        </TabsContent>
      </Tabs>
    </div>
  );
};

export default PresetAdminPage;
