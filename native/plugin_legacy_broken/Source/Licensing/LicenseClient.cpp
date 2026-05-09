#include "LicenseClient.h"

LicenseClient::LicenseClient()
{
    cachedStatus = loadLicenseCache();
}

LicenseStatus LicenseClient::checkLicense()
{
    // 1. Try online verification
    // 2. If offline, check cached license + grace period
    // 3. Return status

    if (!verifyLicenseCacheIntegrity())
    {
        cachedStatus.isValid = false;
        return cachedStatus;
    }

    auto now = juce::Time::getCurrentTime();
    auto daysSinceVerified = (now - cachedStatus.lastVerified).inDays();

    if (daysSinceVerified <= cachedStatus.gracePeriodDays)
    {
        cachedStatus.isOfflineGrace = true;
        cachedStatus.isValid = true;
    }
    else
    {
        cachedStatus.isValid = false;
        cachedStatus.isOfflineGrace = false;
    }

    return cachedStatus;
}

bool LicenseClient::login(const juce::String& email, const juce::String& password)
{
    // TODO: POST to backend /api/auth/login
    // Parse JWT response, store tokens
    // Activate device
    // Cache license

    juce::ignoreUnused(email, password);

    // Stub: simulate successful login
    loggedIn = true;
    cachedStatus.isValid = true;
    cachedStatus.userId = "stub-user-id";
    cachedStatus.plan = "pro";
    cachedStatus.lastVerified = juce::Time::getCurrentTime();
    cachedStatus.machineId = getHardwareFingerprint();

    saveLicenseCache(cachedStatus);
    return true;
}

void LicenseClient::logout()
{
    loggedIn = false;
    authToken = "";
    refreshToken = "";
    cachedStatus = LicenseStatus();
    saveLicenseCache(cachedStatus);
}

juce::String LicenseClient::getHardwareFingerprint() const
{
    // Combine CPU, motherboard, OS info into a hash
    auto systemStats = juce::SystemStats::getOperatingSystemName()
        + juce::SystemStats::getComputerName()
        + juce::String(juce::SystemStats::getNumCpus());
    return juce::String::toHexString((juce::int64) systemStats.hashCode64());
}

bool LicenseClient::activateDevice()
{
    // TODO: POST to backend /api/license/activate
    // Send machineId + auth token
    // Backend checks max device count
    return true;
}

bool LicenseClient::deactivateDevice()
{
    // TODO: POST to backend /api/license/deactivate
    return true;
}

void LicenseClient::refreshEntitlements()
{
    // TODO: GET from backend /api/license/entitlements
    // Update cachedStatus
    // Save to local cache
}

juce::File LicenseClient::getLicenseCacheFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DIDITAGAIN").getChildFile("DIDITAGAIN STUDIO").getChildFile("license.cache");
}

void LicenseClient::saveLicenseCache(const LicenseStatus& status)
{
    auto file = getLicenseCacheFile();
    file.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("isValid", status.isValid);
    obj->setProperty("userId", status.userId);
    obj->setProperty("plan", status.plan);
    obj->setProperty("machineId", status.machineId);
    obj->setProperty("lastVerified", status.lastVerified.toISO8601(true));

    auto json = juce::JSON::toString(juce::var(obj.get()));
    // TODO: Encrypt before saving
    file.replaceWithText(json);
}

LicenseStatus LicenseClient::loadLicenseCache()
{
    LicenseStatus status;
    auto file = getLicenseCacheFile();
    if (!file.existsAsFile()) return status;

    // TODO: Decrypt before parsing
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) return status;

    status.isValid = json.getProperty("isValid", false);
    status.userId = json.getProperty("userId", "").toString();
    status.plan = json.getProperty("plan", "free").toString();
    status.machineId = json.getProperty("machineId", "").toString();
    status.lastVerified = juce::Time::fromISO8601(json.getProperty("lastVerified", "").toString());

    return status;
}

bool LicenseClient::verifyTokenSignature(const juce::String&)
{
    // TODO: Verify JWT signature using public key
    return true;
}

bool LicenseClient::verifyLicenseCacheIntegrity()
{
    // TODO: Check HMAC/signature on cached license file
    auto file = getLicenseCacheFile();
    return file.existsAsFile();
}
