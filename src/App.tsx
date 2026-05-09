import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { BrowserRouter, Route, Routes } from "react-router-dom";
import { Toaster as Sonner } from "@/components/ui/sonner";
import { Toaster } from "@/components/ui/toaster";
import { TooltipProvider } from "@/components/ui/tooltip";
import AdminLayout from "@/components/admin/AdminLayout";
import Index from "./pages/Index";
import UsersPage from "./pages/UsersPage";
import SubscriptionsPage from "./pages/SubscriptionsPage";
import PresetsPage from "./pages/PresetsPage";
import PresetAdminPage from "./pages/PresetAdminPage";
import ActivationsPage from "./pages/ActivationsPage";
import SecurityPage from "./pages/SecurityPage";
import AnnouncementsPage from "./pages/AnnouncementsPage";
import SettingsPage from "./pages/SettingsPage";
import SampleLibraryPage from "./pages/SampleLibraryPage";
import FactorySamplesPage from "./pages/FactorySamplesPage";
import NotFound from "./pages/NotFound";

const queryClient = new QueryClient();

const App = () => (
  <QueryClientProvider client={queryClient}>
    <TooltipProvider>
      <Toaster />
      <Sonner />
      <BrowserRouter>
        <Routes>
          <Route element={<AdminLayout />}>
            <Route path="/" element={<Index />} />
            <Route path="/users" element={<UsersPage />} />
            <Route path="/subscriptions" element={<SubscriptionsPage />} />
            <Route path="/presets" element={<PresetsPage />} />
            <Route path="/presets/admin" element={<PresetAdminPage />} />
            <Route path="/samples" element={<SampleLibraryPage />} />
            <Route path="/activations" element={<ActivationsPage />} />
            <Route path="/security" element={<SecurityPage />} />
            <Route path="/announcements" element={<AnnouncementsPage />} />
            <Route path="/settings" element={<SettingsPage />} />
          </Route>
          <Route path="*" element={<NotFound />} />
        </Routes>
      </BrowserRouter>
    </TooltipProvider>
  </QueryClientProvider>
);

export default App;
