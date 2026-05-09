#pragma once
#include <JuceHeader.h>

struct LicenseStatus
{
    bool isValid = false;
    bool isOfflineGrace = false;
    juce::String userId;
    juce::String plan; // "free", "basic", "pro"
    juce::String machineId;
    juce::Time lastVerified;
    int gracePeriodDays = 7;
};

class LicenseClient
{
public:
    LicenseClient();

    LicenseStatus checkLicense();
    bool login(const juce::String& email, const juce::String& password);
    void logout();
    bool isLoggedIn() const { return loggedIn; }

    juce::String getHardwareFingerprint() const;
    bool activateDevice();
    bool deactivateDevice();

    void refreshEntitlements();
    LicenseStatus getCachedStatus() const { return cachedStatus; }

private:
    LicenseStatus cachedStatus;
    bool loggedIn = false;
    juce::String authToken;
    juce::String refreshToken;

    juce::File getLicenseCacheFile() const;
    void saveLicenseCache(const LicenseStatus& status);
    LicenseStatus loadLicenseCache();

    bool verifyTokenSignature(const juce::String& token);
    bool verifyLicenseCacheIntegrity();
};
