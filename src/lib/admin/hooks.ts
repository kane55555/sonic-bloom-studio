/**
 * React Query bindings for every admin service. Every hook is safe to call
 * even when Lovable Cloud is disabled — queries simply return
 * `CloudDisabledError` so pages can render an empty state via <AsyncBoundary/>.
 */
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { isCloudEnabled } from "./supabaseClient";
import { listUsers, setUserPlan, setUserRole } from "./services/users";
import { listDevices, revokeDevice, reactivateDevice } from "./services/devices";
import {
  listPacks, createPack, updatePack, deletePack, setPackPublished,
  listPackVersions, createPackVersion, deletePackVersion,
} from "./services/packs";
import { listEntitlements, grantPack, revokePack } from "./services/entitlements";
import {
  listAnnouncements, createAnnouncement, updateAnnouncement, deleteAnnouncement,
} from "./services/announcements";
import { listAuditLogs, type AuditFilters } from "./services/auditLogs";
import {
  licenseActivate, licenseVerify, licenseDeactivate, fetchEntitlements,
} from "./licenseFunctions";

const enabled = { enabled: isCloudEnabled() };

// ---- Users ----
export const useUsers = () => useQuery({ queryKey: ["users"], queryFn: listUsers, ...enabled });
export const useSetUserRole = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { userId: string; role: "admin" | "moderator" | "user" }) =>
      setUserRole(v.userId, v.role),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["users"] }),
  });
};
export const useSetUserPlan = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { userId: string; plan: "free" | "basic" | "pro" }) =>
      setUserPlan(v.userId, v.plan),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["users"] }),
  });
};

// ---- Devices ----
export const useDevices = () => useQuery({ queryKey: ["devices"], queryFn: listDevices, ...enabled });
export const useRevokeDevice = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: revokeDevice,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["devices"] }),
  });
};
export const useReactivateDevice = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: reactivateDevice,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["devices"] }),
  });
};

// ---- Packs ----
export const usePacks = () => useQuery({ queryKey: ["packs"], queryFn: listPacks, ...enabled });
export const useCreatePack = () => {
  const qc = useQueryClient();
  return useMutation({ mutationFn: createPack, onSuccess: () => qc.invalidateQueries({ queryKey: ["packs"] }) });
};
export const useUpdatePack = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { id: string; patch: Parameters<typeof updatePack>[1] }) => updatePack(v.id, v.patch),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["packs"] }),
  });
};
export const useSetPackPublished = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { id: string; published: boolean }) => setPackPublished(v.id, v.published),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["packs"] }),
  });
};
export const useDeletePack = () => {
  const qc = useQueryClient();
  return useMutation({ mutationFn: deletePack, onSuccess: () => qc.invalidateQueries({ queryKey: ["packs"] }) });
};

// ---- Pack versions ----
export const usePackVersions = (packId?: string) =>
  useQuery({ queryKey: ["pack_versions", packId ?? "all"], queryFn: () => listPackVersions(packId), ...enabled });
export const useCreatePackVersion = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: createPackVersion,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["pack_versions"] }),
  });
};
export const useDeletePackVersion = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: deletePackVersion,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["pack_versions"] }),
  });
};

// ---- Entitlements ----
export const useEntitlements = () =>
  useQuery({ queryKey: ["entitlements"], queryFn: listEntitlements, ...enabled });
export const useGrantPack = () => {
  const qc = useQueryClient();
  return useMutation({ mutationFn: grantPack, onSuccess: () => qc.invalidateQueries({ queryKey: ["entitlements"] }) });
};
export const useRevokePack = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { userId: string; packId: string }) => revokePack(v.userId, v.packId),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["entitlements"] }),
  });
};

// ---- Announcements ----
export const useAnnouncements = () =>
  useQuery({ queryKey: ["announcements"], queryFn: listAnnouncements, ...enabled });
export const useCreateAnnouncement = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: createAnnouncement,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["announcements"] }),
  });
};
export const useUpdateAnnouncement = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { id: string; patch: Parameters<typeof updateAnnouncement>[1] }) =>
      updateAnnouncement(v.id, v.patch),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["announcements"] }),
  });
};
export const useDeleteAnnouncement = () => {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: deleteAnnouncement,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["announcements"] }),
  });
};

// ---- Audit logs ----
export const useAuditLogs = (filters: AuditFilters = {}) =>
  useQuery({ queryKey: ["audit_logs", filters], queryFn: () => listAuditLogs(filters), ...enabled });

// ---- License functions (Edge Functions) ----
export const useLicenseActivate = () =>
  useMutation({
    mutationFn: (v: { machineId: string; machineName?: string; osInfo?: string }) =>
      licenseActivate(v.machineId, v.machineName, v.osInfo),
  });
export const useLicenseVerify = () =>
  useMutation({ mutationFn: (machineId: string) => licenseVerify(machineId) });
export const useLicenseDeactivate = () =>
  useMutation({ mutationFn: (machineId: string) => licenseDeactivate(machineId) });
export const useEntitlementsFn = () =>
  useQuery({ queryKey: ["entitlements_fn"], queryFn: fetchEntitlements, enabled: false });
