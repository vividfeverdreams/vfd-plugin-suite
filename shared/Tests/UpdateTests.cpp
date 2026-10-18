// Tests for suite updates: versions, GitHub release parsing, checksums, the INFO key's light and the INFO
// page's update row. --shots <folder> saves the page in each state; --live checks GitHub and downloads and
// verifies the latest release's installer for this computer without opening it.
#include "VfdInfo.h"
#include <iostream>

namespace {
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
    if (! ok) ++failures;
}

const juce::String base = "https://github.com/vividfeverdreams/vfd-plugin-suite/releases/download/v9.9.9/";

juce::String releaseJson(const juce::String& tag, bool draft = false, bool prerelease = false,
                         const juce::String& prefix = base, const juce::String& page = "https://github.com/vividfeverdreams/vfd-plugin-suite/releases/tag/v9.9.9")
{
    return "{\"tag_name\":\"" + tag + "\",\"html_url\":\"" + page + "\",\"draft\":" + (draft ? "true" : "false")
         + ",\"prerelease\":" + (prerelease ? "true" : "false") + ",\"assets\":["
           "{\"name\":\"SHA256SUMS.txt\",\"size\":356,\"browser_download_url\":\"" + prefix + "SHA256SUMS.txt\"},"
           "{\"name\":\"Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Windows-VST3.zip\",\"size\":7,\"browser_download_url\":\"" + prefix + "Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Windows-VST3.zip\"},"
           "{\"name\":\"Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Linux-x64.tar.gz\",\"size\":9,\"browser_download_url\":\"" + prefix + "Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Linux-x64.tar.gz\"},"
           "{\"name\":\"Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Install.zip\",\"size\":100,\"browser_download_url\":\"" + prefix + "Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Install.zip\"},"
           "{\"name\":\"Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Windows-Setup.exe\",\"size\":32,\"browser_download_url\":\"" + prefix + "Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Windows-Setup.exe\"}]}";
}

juce::String hashOf(const void* data, size_t size, size_t chunk)
{
    vfd::update::Sha256 hash;
    const auto* bytes = static_cast<const char*>(data);
    for (size_t done = 0; done < size; done += chunk) hash.update(bytes + done, juce::jmin(chunk, size - done));
    return hash.finishHex();
}

void testLogic()
{
    using namespace vfd::update;
    check(compareVersions("1.6.0", "1.5.0") > 0 && compareVersions("v1.5.0", "1.5.0") == 0
              && compareVersions("1.10.0", "1.9.9") > 0 && compareVersions("1.6", "1.6.0") == 0
              && compareVersions("1.6.0-beta", "1.6.0") == 0 && compareVersions("1.5.9", "1.6.0") < 0,
          "versions compare numerically");
    check(isVersion("v1.5.0") && isVersion("2.0") && ! isVersion("latest") && ! isVersion("") && ! isVersion("1.5;rm"),
          "only real version tags count");

    const auto mac = parseRelease(juce::JSON::parse(releaseJson("v9.9.9")), Platform::mac);
    check(mac.version == "9.9.9" && mac.assetName == "Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Install.zip" && mac.assetSize == 100
              && mac.sumsUrl == base + "SHA256SUMS.txt" && mac.canInstall(),
          "Mac picks the installer zip and the checksums");
    const auto windows = parseRelease(juce::JSON::parse(releaseJson("v9.9.9")), Platform::windows);
    check(windows.assetName == "Vivid-Fever-Dreams-Suite-v9.9.9-Beta-Windows-Setup.exe" && windows.assetSize == 32,
          "Windows picks Setup.exe, not the VST3 zip");
    const auto linuxRelease = parseRelease(juce::JSON::parse(releaseJson("v9.9.9")), Platform::other);
    check(linuxRelease.version == "9.9.9" && ! linuxRelease.canInstall() && linuxRelease.sumsUrl == base + "SHA256SUMS.txt",
          "Linux sees the release but has no installer to open (GET opens the releases page)");
    check(! parseRelease(juce::JSON::parse(releaseJson("v9.9.9", true)), Platform::mac).isKnown()
              && ! parseRelease(juce::JSON::parse(releaseJson("v9.9.9", false, true)), Platform::mac).isKnown(),
          "drafts and pre-releases are ignored");
    const auto foreign = parseRelease(juce::JSON::parse(releaseJson("v9.9.9", false, false, "https://example.com/files/",
                                                                    "https://example.com/release")), Platform::mac);
    check(foreign.isKnown() && ! foreign.canInstall() && foreign.sumsUrl.isEmpty() && foreign.page == releasesPage(),
          "files and pages outside the suite's GitHub releases are never used");
    check(! isInstallerAsset("../Install.zip", Platform::mac) && ! isInstallerAsset("a/b-Install.zip", Platform::mac)
              && ! isInstallerAsset(".hidden-Install.zip", Platform::mac) && ! isInstallerAsset("Suite-Setup.exe", Platform::mac),
          "installer names must be plain file names for this platform");
    check(! parseRelease(juce::JSON::parse("{\"message\":\"Not Found\"}"), Platform::mac).isKnown()
              && ! parseRelease(juce::JSON::parse("not json"), Platform::mac).isKnown(),
          "GitHub errors and junk name no release");

    // The cache keeps an installer only for this computer, so round-trip this platform's answer.
    const auto local = parseRelease(juce::JSON::parse(releaseJson("v9.9.9")), thisPlatform());
    const auto restored = Release::fromVar(juce::JSON::parse(juce::JSON::toString(local.toVar())));
    check(restored.version == local.version && restored.assetUrl == local.assetUrl && restored.assetSize == local.assetSize
              && restored.sumsUrl == local.sumsUrl && restored.page == local.page,
          "the shared answer survives the cache file");
    auto tampered = mac.toVar();
    tampered.getDynamicObject()->setProperty("assetUrl", "https://example.com/x-Install.zip");
    check(! Release::fromVar(tampered).canInstall(), "a cache file can't point INSTALL elsewhere");

    const juce::String sums = "f461003a66dfe0669538e1a85c8a69c7593c368af8e075f1a37516a8031912fc  Vivid-Fever-Dreams-Suite-v1.5.0-Beta-Install.zip\n"
                              "76deed6b7baaa416eb9adb956c4246573e65cb437845daf72c80350062b2e0fc *Vivid-Fever-Dreams-Suite-v1.5.0-Beta-Windows-Setup.exe\n"
                              "nothex  broken.zip\n";
    check(listedChecksum(sums, "Vivid-Fever-Dreams-Suite-v1.5.0-Beta-Install.zip") == "f461003a66dfe0669538e1a85c8a69c7593c368af8e075f1a37516a8031912fc"
              && listedChecksum(sums, "Vivid-Fever-Dreams-Suite-v1.5.0-Beta-Windows-Setup.exe") == "76deed6b7baaa416eb9adb956c4246573e65cb437845daf72c80350062b2e0fc"
              && listedChecksum(sums, "broken.zip").isEmpty() && listedChecksum(sums, "missing.zip").isEmpty(),
          "checksums are read from SHA256SUMS.txt");

    check(hashOf("", 0, 64) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
              && hashOf("abc", 3, 64) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 matches the FIPS test vectors");
    const std::string million(1000000, 'a');
    check(hashOf(million.data(), million.size(), 4093) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 of a million 'a's, fed in odd chunks");
    juce::MemoryBlock random(300001);
    juce::Random generator(7);
    generator.fillBitsRandomly(random.getData(), random.getSize());
    const auto file = juce::File::createTempFile("vfd-sha");
    file.replaceWithData(random.getData(), random.getSize());
    juce::ChildProcess shasum;
    shasum.start(juce::StringArray { "/usr/bin/shasum", "-a", "256", file.getFullPathName() });
    const auto reference = shasum.readAllProcessOutput().upToFirstOccurrenceOf(" ", false, false).trim();
    check(reference.length() == 64 && hashOf(random.getData(), random.getSize(), 65536) == reference
              && hashOf(random.getData(), random.getSize(), 1) == reference,
          "SHA-256 agrees with shasum");
    file.deleteFile();
}

juce::Image render(juce::Component& component)
{
    return component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
}

void save(const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
}

juce::Component* findNamed(juce::Component& parent, const juce::String& name)
{
    if (parent.getName() == name) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* found = findNamed(*child, name)) return found;
    return nullptr;
}

void testInterface(const juce::File& shots)
{
    using namespace vfd::update;
    juce::MemoryBlock fontData, logo;
    juce::File(VFD_FONT_FILE).loadFileAsData(fontData);
    juce::File(VFD_LOGO_FILE).loadFileAsData(logo);
    const auto face = juce::Typeface::createSystemTypefaceFor(fontData.getData(), fontData.getSize());
    juce::SharedResourcePointer<Service> service;
    for (const auto& name : { juce::String("YOI-BOI"), juce::String("SHIFTY") }) {
        const auto theme = vfd::themeFor(name);
        vfd::InfoButton key(theme, face);
        key.setBounds(0, 0, 96, 38);
        vfd::InfoOverlay page(name, theme, static_cast<const char*>(logo.getData()), logo.getSize(), face);
        page.setBounds(0, 0, 518, 691);
        page.open(&key);
        auto* button = dynamic_cast<juce::Button*>(findNamed(page, "Suite update"));
        check(button != nullptr && button->isShowing() == false && button->getParentComponent() != nullptr
                  && button->getParentComponent()->getName() == "Suite updates",
              name + ": the update button sits in its row on the INFO page");
        const auto shot = [&](const juce::String& state) {
            service->dispatchPendingMessages();
            if (shots != juce::File()) save(render(page), shots.getChildFile(name + "-info-" + state + ".png"));
        };
        service->showStageForTesting(Service::Stage::idle, {});
        shot("idle");
        check(button->getButtonText() == "CHECK FOR UPDATES" && ! key.showsUpdate(), name + ": starts with CHECK FOR UPDATES");
        Release newer = parseRelease(juce::JSON::parse(releaseJson("v9.9.9")), thisPlatform());
        service->applyForTesting(newer);
        service->dispatchPendingMessages();
        const juce::String offer = newer.canInstall() ? "INSTALL 9.9.9" : "GET 9.9.9"; // Linux: no installer
        check(key.showsUpdate() && button->getButtonText() == offer, name + ": a newer release lights INFO and offers " + offer);
        shot("available");
        if (shots != juce::File()) save(key.createComponentSnapshot(key.getLocalBounds(), true, 4.0f), shots.getChildFile(name + "-info-key-update.png"));
        service->showStageForTesting(Service::Stage::downloading, "Downloading the installer. Keep this window open.", 0.42f);
        service->dispatchPendingMessages();
        check(button->getButtonText() == "DOWNLOADING 42%", name + ": shows download progress");
        shot("downloading");
        service->showStageForTesting(Service::Stage::opened, "The installer is open. Quit your DAW, click Install, then reopen your project.");
        shot("opened");
        service->showStageForTesting(Service::Stage::failed, "The download doesn't match the release's checksum, so it wasn't opened.", 0, true);
        service->dispatchPendingMessages();
        check(button->getButtonText() == "OPEN RELEASES PAGE", name + ": a failed install offers the releases page");
        shot("failed");
        Release older = newer;
        older.version = "1.0.0";
        service->applyForTesting(older);
        service->dispatchPendingMessages();
        check(! key.showsUpdate() && button->getButtonText() == "CHECK AGAIN", name + ": an older release leaves INFO dark");
        shot("up-to-date");
        // Keyboard: Tab reaches the update button last.
        const auto order = page.keyPressed(juce::KeyPress(juce::KeyPress::tabKey));
        check(order, name + ": Tab is handled on the page");
    }
}

void testLive()
{
    using namespace vfd::update;
    juce::SharedResourcePointer<Service> service;
    service->checkNow();
    for (int i = 0; i < 300 && service->getStatus().stage == Service::Stage::checking; ++i) juce::Thread::sleep(100);
    for (int i = 0; i < 300 && (service->getStatus().stage == Service::Stage::idle || service->getStatus().stage == Service::Stage::checking); ++i)
        juce::Thread::sleep(100);
    const auto latest = service->getRelease();
    std::cout << "GitHub latest: " << latest.version << " (" << latest.assetName << ", " << latest.assetSize << " bytes); installed "
              << Service::installedVersion() << "; status: " << service->getStatus().message << '\n';
    check(Service::updatesFolder().getChildFile("latest-release.json").existsAsFile(), "live: the answer is cached for the other plug-ins");
    if (thisPlatform() == Platform::other) {
        check(latest.isKnown() && latest.sumsUrl.isNotEmpty(), "live: GitHub names the latest release (Linux gets it from the releases page)");
        return;
    }
    check(latest.canInstall() && latest.sumsUrl.isNotEmpty(), "live: GitHub names the latest release, its installer and checksums");
    const auto started = juce::Time::getMillisecondCounterHiRes();
    juce::String error;
    const auto installer = service->prepareForTesting(latest, error);
    std::cout << "prepared in " << juce::roundToInt((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0) << " s: "
              << installer.getFullPathName() << " " << error << '\n';
   #if JUCE_MAC
    check(installer.isDirectory() && installer.getFileName() == "Install Vivid Fever Dreams Suite.app",
          "live: the Mac installer downloads, matches its checksum, unzips and passes the signature and notarization checks");
   #else
    check(installer.existsAsFile(), "live: the installer downloads and matches its checksum");
   #endif
    Service::updatesFolder().getChildFile(latest.version).deleteRecursively();
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(argv[i]);
    juce::File shots;
    if (const int at = args.indexOf("--shots"); at >= 0 && at + 1 < args.size()) {
        shots = juce::File(args[at + 1]);
        shots.createDirectory();
    }
    testLogic();
    testInterface(shots);
    if (args.contains("--live")) testLive();
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String(failures) + " FAILED") << '\n';
    return failures == 0 ? 0 : 1;
}
