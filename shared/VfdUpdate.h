#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <cstring>
#include "VfdSuiteVersion.h"

// Suite updates from the GitHub releases page, for the INFO page.
//
// An open editor asks for a check once per session; the answer is cached for a day and shared by every VFD
// plug-in on the computer. INSTALL downloads this platform's installer from the latest release, verifies it
// (the release's SHA-256 checksum, and on the Mac the Developer ID signature and notarization) and opens it.
// Linux has no installer to open, so there the newer release's page is offered instead. The installer
// replaces the plug-ins and refuses to while a DAW that loads them is running, so nothing here touches a
// loaded plug-in. Everything runs on one background thread owned by the open editors; closing the
// last editor cancels it. No audio state is read or changed.
namespace vfd::update {

inline const juce::String repository { "vividfeverdreams/vfd-plugin-suite" };
inline juce::String latestReleaseApi() { return "https://api.github.com/repos/" + repository + "/releases/latest"; }
inline juce::String releasesPage() { return "https://github.com/" + repository + "/releases/latest"; }
inline juce::String downloadPrefix() { return "https://github.com/" + repository + "/releases/download/"; }
// The Apple Developer team that signs the Mac installer.
inline const juce::String macTeam { "LZXM44W38L" };

// Versions ------------------------------------------------------------------------------------------------------

/** "v1.6.0", "1.6", "1.6.0-beta" -> {1, 6, 0}. */
inline std::array<int, 3> versionParts(juce::String version)
{
    version = version.trim().trimCharactersAtStart("vV").upToFirstOccurrenceOf("-", false, false)
                     .upToFirstOccurrenceOf("+", false, false);
    std::array<int, 3> parts {};
    const auto tokens = juce::StringArray::fromTokens(version, ".", "");
    for (int i = 0; i < 3 && i < tokens.size(); ++i) parts[static_cast<size_t>(i)] = tokens[i].getIntValue();
    return parts;
}

inline bool isVersion(const juce::String& text)
{
    const auto version = text.trim().trimCharactersAtStart("vV");
    return version.isNotEmpty() && juce::CharacterFunctions::isDigit(version[0])
        && version.upToFirstOccurrenceOf("-", false, false).containsOnly("0123456789.");
}

/** Negative when a is older than b, positive when newer, zero when the same release. */
inline int compareVersions(const juce::String& a, const juce::String& b)
{
    const auto x = versionParts(a), y = versionParts(b);
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

// Releases ------------------------------------------------------------------------------------------------------

enum class Platform { mac, windows, other };

inline Platform thisPlatform()
{
   #if JUCE_MAC
    return Platform::mac;
   #elif JUCE_WINDOWS
    return Platform::windows;
   #else
    return Platform::other;
   #endif
}

/** The suite installer for a platform: the Mac "...-Install.zip" or the Windows "...-Setup.exe". */
inline bool isInstallerAsset(const juce::String& name, Platform platform)
{
    if (name.isEmpty() || name != juce::File::createLegalFileName(name) || name.startsWithChar('.')) return false;
    if (platform == Platform::mac) return name.endsWithIgnoreCase("-Install.zip") && ! name.containsIgnoreCase("Windows");
    if (platform == Platform::windows) return name.endsWithIgnoreCase("-Setup.exe");
    return false;
}

struct Release {
    juce::String version, page, assetName, assetUrl, sumsUrl;
    juce::int64 assetSize = 0;

    bool isKnown() const { return isVersion(version); }
    bool canInstall() const { return isKnown() && assetUrl.isNotEmpty(); }

    juce::var toVar() const
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("version", version);
        object->setProperty("page", page);
        object->setProperty("assetName", assetName);
        object->setProperty("assetUrl", assetUrl);
        object->setProperty("assetSize", assetSize);
        object->setProperty("sumsUrl", sumsUrl);
        return juce::var(object);
    }

    static Release fromVar(const juce::var& value)
    {
        Release release;
        release.version = value.getProperty("version", {}).toString();
        release.page = value.getProperty("page", {}).toString();
        release.assetName = value.getProperty("assetName", {}).toString();
        release.assetUrl = value.getProperty("assetUrl", {}).toString();
        release.assetSize = static_cast<juce::int64>(value.getProperty("assetSize", 0));
        release.sumsUrl = value.getProperty("sumsUrl", {}).toString();
        // A cache file is checked like a fresh answer from GitHub.
        if (! release.isKnown() || ! release.page.startsWith("https://github.com/" + repository + "/")) return {};
        if (! release.assetUrl.startsWith(downloadPrefix()) || ! isInstallerAsset(release.assetName, thisPlatform()))
            release.assetUrl = release.assetName = {};
        if (! release.sumsUrl.startsWith(downloadPrefix())) release.sumsUrl = {};
        return release;
    }
};

/** Reads GitHub's latest-release answer: the version, its page, this platform's installer and SHA256SUMS.txt.
    Only files attached to this repository's releases are used. */
inline Release parseRelease(const juce::var& json, Platform platform)
{
    if (! json.isObject() || static_cast<bool>(json.getProperty("draft", false))
        || static_cast<bool>(json.getProperty("prerelease", false)))
        return {};
    Release release;
    release.version = json.getProperty("tag_name", {}).toString().trim().trimCharactersAtStart("vV");
    if (! release.isKnown()) return {};
    const auto page = json.getProperty("html_url", {}).toString();
    release.page = page.startsWith("https://github.com/" + repository + "/releases/") ? page : releasesPage();
    if (const auto* assets = json.getProperty("assets", {}).getArray())
        for (const auto& asset : *assets) {
            const auto name = asset.getProperty("name", {}).toString();
            const auto url = asset.getProperty("browser_download_url", {}).toString();
            if (! url.startsWith(downloadPrefix())) continue;
            if (name == "SHA256SUMS.txt") release.sumsUrl = url;
            else if (release.assetUrl.isEmpty() && isInstallerAsset(name, platform)) {
                release.assetName = name;
                release.assetUrl = url;
                release.assetSize = static_cast<juce::int64>(asset.getProperty("size", 0));
            }
        }
    return release;
}

/** The SHA-256 that a SHA256SUMS.txt ("<hex>  <file>" per line) lists for `name`, or empty. */
inline juce::String listedChecksum(const juce::String& sums, const juce::String& name)
{
    for (auto line : juce::StringArray::fromLines(sums)) {
        line = line.trim();
        const auto hash = line.upToFirstOccurrenceOf(" ", false, false).toLowerCase();
        auto file = line.fromFirstOccurrenceOf(" ", false, false).trim();
        if (file.startsWithChar('*')) file = file.substring(1);
        if (file == name && hash.length() == 64 && hash.containsOnly("0123456789abcdef")) return hash;
    }
    return {};
}

// SHA-256 (FIPS 180-4), streamed -----------------------------------------------------------------------------------

class Sha256 {
public:
    void update(const void* data, size_t size)
    {
        const auto* bytes = static_cast<const juce::uint8*>(data);
        total += size;
        while (size > 0) {
            const size_t take = juce::jmin(size, static_cast<size_t>(64) - used);
            std::memcpy(block.data() + used, bytes, take);
            used += take; bytes += take; size -= take;
            if (used == 64) { compress(); used = 0; }
        }
    }

    juce::String finishHex()
    {
        const juce::uint64 bits = static_cast<juce::uint64>(total) * 8;
        const juce::uint8 one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56) update(&zero, 1);
        juce::uint8 length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<juce::uint8>(bits >> (56 - 8 * i));
        update(length, 8);
        juce::String hex;
        for (const auto word : state) hex << juce::String::toHexString(word).paddedLeft('0', 8);
        return hex;
    }

private:
    static juce::uint32 rotate(juce::uint32 x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress()
    {
        static constexpr juce::uint32 k[64] {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        juce::uint32 w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (juce::uint32(block[size_t(4 * i)]) << 24) | (juce::uint32(block[size_t(4 * i + 1)]) << 16)
                 | (juce::uint32(block[size_t(4 * i + 2)]) << 8) | juce::uint32(block[size_t(4 * i + 3)]);
        for (int i = 16; i < 64; ++i) {
            const auto s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const auto t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    std::array<juce::uint32, 8> state { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::array<juce::uint8, 64> block {};
    size_t used = 0;
    juce::uint64 total = 0;
};

// The service ---------------------------------------------------------------------------------------------------

/** Shared by the INFO key and page of every open editor of a plug-in (juce::SharedResourcePointer). */
class Service final : public juce::ChangeBroadcaster, private juce::AsyncUpdater, private juce::Thread {
public:
    enum class Stage { idle, checking, upToDate, available, downloading, verifying, opened, failed };
    struct Status {
        Stage stage = Stage::idle;
        juce::String message;
        float progress = 0.0f;       // download, 0..1
        bool installFailed = false;  // a failed install offers the releases page; a failed check, another try
    };

    Service() : juce::Thread("VFD update") {}

    ~Service() override
    {
        cancelled = true;
        cancelNetwork();
        cancelPendingUpdate();
        stopThread(20000);
    }

    static juce::String installedVersion() { return VFD_SUITE_VERSION; }

    Status getStatus() const { const juce::ScopedLock sl(lock); return status; }
    Release getRelease() const { const juce::ScopedLock sl(lock); return release; }

    /** True when GitHub's latest release is newer than these plug-ins. */
    bool hasUpdate() const
    {
        const juce::ScopedLock sl(lock);
        return release.isKnown() && compareVersions(release.version, installedVersion()) > 0;
    }

    /** Once per session: uses the shared answer if it is less than a day old, otherwise asks GitHub. Failures
        stay quiet. */
    void checkAutomatically()
    {
        if (! automaticCheckDone.exchange(true)) start(Job::automaticCheck);
    }

    /** CHECK FOR UPDATES: always asks GitHub and reports what happened. */
    void checkNow() { start(Job::check); }

    /** Downloads, verifies and opens the newer release's installer. */
    void install()
    {
        if (hasUpdate() && getRelease().canInstall()) start(Job::install);
    }

    void openReleasePage() const
    {
        const auto page = getRelease().page;
        juce::URL(page.isNotEmpty() ? page : releasesPage()).launchInDefaultBrowser();
    }

    void showInstaller() const
    {
        juce::File installer;
        { const juce::ScopedLock sl(lock); installer = installerFile; }
        if (installer.exists()) installer.revealToUser();
        else updatesFolder().revealToUser();
    }

    /** Where updates are downloaded and the shared answer is kept. */
    static juce::File updatesFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Vivid Fever Dreams/Updates");
       #elif JUCE_WINDOWS
        return juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getChildFile("Vivid Fever Dreams/Updates");
       #else
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Vivid Fever Dreams/Updates");
       #endif
    }

    /** Test hooks: show a release as if GitHub had answered, or show a stage. */
    void applyForTesting(const Release& latest) { apply(latest, true); }
    void showStageForTesting(Stage stage, const juce::String& message, float progress = 0.0f, bool installFailed = false)
    {
        { const juce::ScopedLock sl(lock); status = { stage, message, progress, installFailed }; }
        sendChangeMessage();
    }

    /** Test hook: download, verify and prepare `forRelease` without opening it; returns the installer or an error. */
    juce::File prepareForTesting(const Release& forRelease, juce::String& error)
    {
        { const juce::ScopedLock sl(lock); release = forRelease; }
        const auto installer = prepare(forRelease, error);
        return installer;
    }

private:
    enum class Job { automaticCheck, check, install };

    void start(Job job)
    {
        if (working.exchange(true)) return; // one job at a time
        { const juce::ScopedLock sl(lock); pending = job; }
        if (isThreadRunning()) stopThread(2000); // the previous job has finished; let its thread exit
        if (! startThread(juce::Thread::Priority::low)) working = false;
    }

    void run() override
    {
        Job job;
        { const juce::ScopedLock sl(lock); job = pending; }
        if (job == Job::install) runInstall();
        else runCheck(job == Job::check);
        working = false;
        sendChangeMessage();
    }

    // Checking ------------------------------------------------------------------------------------------------

    void runCheck(bool manual)
    {
        if (manual) setStatus(Stage::checking, "Checking GitHub for a newer release...");
        const auto cache = juce::JSON::parse(cacheFile());
        const auto checked = static_cast<juce::int64>(cache.getProperty("checked", 0));
        const auto now = juce::Time::currentTimeMillis();
        const bool fresh = now - checked >= 0 && now - checked < 20 * 60 * 60 * 1000;
        Release latest;
        if (! manual && fresh) {
            latest = Release::fromVar(cache.getProperty("release", {}));
        } else {
            juce::MemoryOutputStream answer;
            juce::String error;
            if (! fetch(latestReleaseApi(), "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
                        answer, 4 * 1024 * 1024, 0, false, error)) {
                if (manual) { fail("Couldn't reach GitHub (" + error + ").", false); return; }
                latest = Release::fromVar(cache.getProperty("release", {})); // offline: keep the last answer
            } else {
                latest = parseRelease(juce::JSON::parse(answer.toString()), thisPlatform());
                if (latest.isKnown()) {
                    auto* object = new juce::DynamicObject();
                    object->setProperty("checked", now);
                    object->setProperty("release", latest.toVar());
                    if (updatesFolder().createDirectory().wasOk())
                        cacheFile().replaceWithText(juce::JSON::toString(juce::var(object)));
                } else if (manual) { fail("GitHub's answer didn't name a release.", false); return; }
            }
        }
        apply(latest, manual);
    }

    void apply(const Release& latest, bool manual)
    {
        { const juce::ScopedLock sl(lock); release = latest; }
        if (hasUpdate())
            setStatus(Stage::available, latest.canInstall()
                ? "Suite " + latest.version + " is out. INSTALL downloads its installer and opens it."
                : "Suite " + latest.version + " is out. Get it from the releases page.");
        else if (latest.isKnown() || manual)
            setStatus(Stage::upToDate, "You have the latest release.");
        else
            setStatus(Stage::idle, {});
    }

    static juce::File cacheFile() { return updatesFolder().getChildFile("latest-release.json"); }

    // Installing ----------------------------------------------------------------------------------------------

    void runInstall()
    {
        juce::String error;
        const auto installer = prepare(getRelease(), error);
        if (installer == juce::File()) { fail(error, true); return; }
        { const juce::ScopedLock sl(lock); installerFile = installer; }
        if (! cancelled) triggerAsyncUpdate(); // opens it on the message thread
    }

    /** Downloads and verifies the installer; on the Mac it is unzipped first. */
    juce::File prepare(const Release& target, juce::String& error)
    {
        if (! target.canInstall()) { error = "This release has no installer for this computer."; return {}; }
        const auto root = updatesFolder();
        // Earlier downloads are ours to replace.
        for (const auto& old : root.findChildFiles(juce::File::findDirectories, false))
            old.deleteRecursively();
        const auto folder = root.getChildFile(target.version);
        if (! folder.createDirectory().wasOk()) { error = "Couldn't create " + folder.getFullPathName() + "."; return {}; }
        const auto download = folder.getChildFile(target.assetName);
        const auto partial = folder.getChildFile(target.assetName + ".part");

        setStatus(Stage::downloading, "Downloading the installer. Keep this window open.", 0.0f);
        {
            juce::FileOutputStream out(partial);
            if (! out.openedOk()) { error = "Couldn't save the download in " + folder.getFullPathName() + "."; return {}; }
            juce::String reason;
            if (! fetch(target.assetUrl, "Accept: application/octet-stream\r\n", out, juce::int64(1) << 30,
                        target.assetSize, true, reason)) {
                error = "The download stopped (" + reason + ").";
                return {};
            }
            out.flush();
            if (out.getStatus().failed()) { error = "Couldn't save the download: " + out.getStatus().getErrorMessage(); return {}; }
        }
        if ((target.assetSize > 0 && partial.getSize() != target.assetSize) || ! partial.moveFileTo(download)) {
            error = "The download was incomplete.";
            return {};
        }

        setStatus(Stage::verifying, "Checking the download...");
        if (target.sumsUrl.isNotEmpty()) {
            juce::MemoryOutputStream sums;
            juce::String reason;
            if (! fetch(target.sumsUrl, {}, sums, 1 << 20, 0, false, reason)) { error = "Couldn't read the release's checksums (" + reason + ")."; return {}; }
            const auto expected = listedChecksum(sums.toString(), target.assetName);
            if (expected.isEmpty()) { error = "The release's checksums don't list " + target.assetName + "."; return {}; }
            if (sha256(download) != expected) { error = "The download doesn't match the release's checksum, so it wasn't opened."; return {}; }
        }
       #if JUCE_MAC
        return unpackMacInstaller(download, folder, error);
       #else
        return download;
       #endif
    }

   #if JUCE_MAC
    /** Unzips the Mac release with ditto (as Finder does, keeping the signature intact) and checks the installer
        is signed by the suite's Developer ID team and notarized, as Gatekeeper would. */
    juce::File unpackMacInstaller(const juce::File& zip, const juce::File& folder, juce::String& error)
    {
        const auto expanded = folder.getChildFile("expanded");
        if (runTool({ "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), expanded.getFullPathName() }, 180000) != 0) {
            error = "Couldn't unzip the download.";
            return {};
        }
        zip.deleteFile();
        juce::File app;
        for (const auto& item : expanded.findChildFiles(juce::File::findDirectories, false))
            for (const auto& inner : (item.hasFileExtension("app") ? juce::Array<juce::File> { item }
                                                                   : item.findChildFiles(juce::File::findDirectories, false, "*.app")))
                if (app == juce::File() && inner.getFileName().startsWithIgnoreCase("Install")) app = inner;
        if (app == juce::File()) { error = "The download has no installer app."; return {}; }
        const auto requirement = "-R=anchor apple generic and certificate leaf[subject.OU] = \"" + macTeam + "\"";
        if (runTool({ "/usr/bin/codesign", "--verify", "--deep", "--strict", requirement, app.getFullPathName() }, 120000) != 0) {
            error = "The installer isn't signed by Vivid Fever Dreams, so it wasn't opened.";
            return {};
        }
        if (runTool({ "/usr/sbin/spctl", "--assess", "--type", "execute", app.getFullPathName() }, 120000) != 0) {
            error = "macOS didn't accept the installer (notarization), so it wasn't opened.";
            return {};
        }
        return app;
    }

    /** Runs a system tool, without a shell, and returns its exit code (-1 if it couldn't run or was stopped). */
    int runTool(const juce::StringArray& command, int timeoutMs)
    {
        juce::ChildProcess process;
        if (! process.start(command, 0)) return -1;
        for (int waited = 0; ! process.waitForProcessToFinish(100); waited += 100)
            if (cancelled || threadShouldExit() || waited > timeoutMs) { process.kill(); return -1; }
        return static_cast<int>(process.getExitCode());
    }
   #endif

    void handleAsyncUpdate() override
    {
        juce::File installer;
        { const juce::ScopedLock sl(lock); installer = installerFile; }
        if (installer.exists() && installer.startAsProcess()) {
           #if JUCE_MAC
            setStatus(Stage::opened, "The installer is open. Quit your DAW, click Install, then reopen your project.");
           #else
            setStatus(Stage::opened, "The installer is open. Save your project and close your DAW before installing.");
           #endif
        } else {
            showInstaller();
            fail("The installer didn't open by itself, so its folder is shown instead.", true);
        }
    }

    // Network and files ---------------------------------------------------------------------------------------

    /** Reads an https address into `out`, cancellable from any thread. `expected` (if known) and `limit` bound the
        size; progress is reported when asked. */
    bool fetch(const juce::String& address, const juce::String& headers, juce::OutputStream& out, juce::int64 limit,
               juce::int64 expected, bool reportProgress, juce::String& error)
    {
        juce::WebInputStream stream(juce::URL(address), false);
        stream.withExtraHeaders("User-Agent: VividFeverDreams-Suite/" + installedVersion() + "\r\n" + headers)
              .withConnectionTimeout(20000);
        {
            const juce::ScopedLock sl(streamLock);
            if (cancelled || threadShouldExit()) { error = "cancelled"; return false; }
            activeStream = &stream;
        }
        const juce::ScopeGuard forget { [this] { const juce::ScopedLock sl(streamLock); activeStream = nullptr; } };
        if (! stream.connect(nullptr)) { error = cancelled ? "cancelled" : "no connection"; return false; }
        if (const int code = stream.getStatusCode(); code != 200) { error = "HTTP " + juce::String(code); return false; }
        auto total = stream.getTotalLength();
        if (total <= 0) total = expected;
        if (total > limit) { error = "too large"; return false; }
        juce::HeapBlock<char> buffer(1 << 16);
        juce::int64 done = 0;
        float shown = 0.0f;
        for (;;) {
            if (cancelled || threadShouldExit()) { error = "cancelled"; return false; }
            const int read = stream.read(buffer, 1 << 16);
            if (read <= 0) break;
            done += read;
            if (done > limit) { error = "too large"; return false; }
            if (! out.write(buffer, static_cast<size_t>(read))) { error = "couldn't save the file"; return false; }
            if (reportProgress && total > 0) {
                const auto fraction = static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
                if (fraction - shown >= 0.01f) { shown = fraction; setProgress(fraction); }
            }
        }
        if (cancelled || stream.isError() || (total > 0 && done != total)) { error = cancelled ? "cancelled" : "incomplete"; return false; }
        return true;
    }

    void cancelNetwork()
    {
        const juce::ScopedLock sl(streamLock);
        if (activeStream != nullptr) activeStream->cancel();
    }

    juce::String sha256(const juce::File& file)
    {
        juce::FileInputStream in(file);
        if (! in.openedOk()) return {};
        Sha256 hash;
        juce::HeapBlock<char> buffer(1 << 16);
        for (int read; (read = in.read(buffer, 1 << 16)) > 0;) {
            if (cancelled || threadShouldExit()) return {};
            hash.update(buffer, static_cast<size_t>(read));
        }
        return hash.finishHex();
    }

    // Status --------------------------------------------------------------------------------------------------

    void setStatus(Stage stage, const juce::String& message, float progress = 0.0f)
    {
        { const juce::ScopedLock sl(lock); status = { stage, message, progress, false }; }
        sendChangeMessage();
    }

    void setProgress(float progress)
    {
        { const juce::ScopedLock sl(lock); status.progress = progress; }
        sendChangeMessage();
    }

    void fail(const juce::String& message, bool duringInstall)
    {
        { const juce::ScopedLock sl(lock); status = { Stage::failed, message, 0.0f, duringInstall }; }
        sendChangeMessage();
    }

    juce::CriticalSection lock, streamLock;
    Status status;
    Release release;
    juce::File installerFile;
    Job pending = Job::automaticCheck;
    juce::WebInputStream* activeStream = nullptr;
    std::atomic<bool> working { false }, cancelled { false }, automaticCheckDone { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Service)
};

} // namespace vfd::update
