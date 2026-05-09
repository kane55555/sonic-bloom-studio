import { CropLibrary } from "@/components/admin/CropLibrary";

export default function SampleLibraryPage() {
  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-bold">User Samples</h1>
        <p className="text-sm text-muted-foreground">
          Import your own audio and crop / loop each one. All edits are saved
          automatically and persist in your browser.
        </p>
      </div>
      <CropLibrary
        storageKey="dida.userCrop.v1"
        allowUpload={true}
        emptyMessage='No samples yet. Click the upload icon to import audio.'
      />
    </div>
  );
}
