// TrackDownloader.cpp

#include "TrackDownloader.h"

#include "Gunzip.h"
#include "Sha256.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

// Largest decompressed track we accept (the biggest real one is under 3 MB).
const std::size_t kMaxTrackBytes = 64u * 1024u * 1024u;

bool IsSafeShard(const std::string& pShard)
{
    if (pShard.empty() || pShard.size() > 64 || pShard[0] == '.') {
        return false;
    }
    for (char lChar : pShard) {
        const bool lOk = (lChar >= 'a' && lChar <= 'z') || (lChar >= 'A' && lChar <= 'Z') ||
                         (lChar >= '0' && lChar <= '9') || lChar == '-' || lChar == '_' || lChar == '.';
        if (!lOk) {
            return false;
        }
    }
    return true;
}

bool IsHex(const std::string& pText, std::size_t pMin, std::size_t pMax)
{
    if (pText.size() < pMin || pText.size() > pMax) {
        return false;
    }
    for (char lChar : pText) {
        if (!((lChar >= '0' && lChar <= '9') || (lChar >= 'a' && lChar <= 'f'))) {
            return false;
        }
    }
    return true;
}

long long FileSize(const std::string& pPath)
{
    std::ifstream lFile(pPath.c_str(), std::ios::binary | std::ios::ate);
    if (!lFile.good()) {
        return -1;
    }
    return static_cast<long long>(lFile.tellg());
}

bool ReplaceFile(const std::string& pFrom, const std::string& pTo)
{
#ifdef _WIN32
    return MoveFileExA(pFrom.c_str(), pTo.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(pFrom.c_str(), pTo.c_str()) == 0;
#endif
}

// Result of running one external downloader.
// eRejected: the server answered but refused (HTTP 4xx/5xx, e.g. 404).
enum class RunResult { eOk, eFailed, eRejected, eNotFound, eCancelled };

#ifdef _WIN32

// Quotes one argument for a Windows command line.
std::string QuoteArgument(const std::string& pArgument)
{
    std::string lQuoted = "\"";
    for (char lChar : pArgument) {
        if (lChar == '"') lQuoted += '\\';
        lQuoted += lChar;
    }
    return lQuoted + "\"";
}

RunResult RunDownloader(const std::vector<std::string>& pArguments, const std::string& pOutputPath,
                        const std::atomic<bool>& pCancel, std::atomic<long long>& pBytesDone)
{
    std::string lCommandLine;
    for (const std::string& lArgument : pArguments) {
        if (!lCommandLine.empty()) lCommandLine += ' ';
        lCommandLine += QuoteArgument(lArgument);
    }
    STARTUPINFOA lStartup;
    ZeroMemory(&lStartup, sizeof(lStartup));
    lStartup.cb = sizeof(lStartup);
    lStartup.dwFlags = STARTF_USESHOWWINDOW;
    lStartup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION lProcess;
    ZeroMemory(&lProcess, sizeof(lProcess));
    std::vector<char> lMutable(lCommandLine.begin(), lCommandLine.end());
    lMutable.push_back('\0');
    if (!CreateProcessA(nullptr, lMutable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &lStartup, &lProcess)) {
        return RunResult::eNotFound;
    }
    RunResult lResult = RunResult::eFailed;
    while (true) {
        const DWORD lWait = WaitForSingleObject(lProcess.hProcess, 100);
        pBytesDone = FileSize(pOutputPath) > 0 ? FileSize(pOutputPath) : 0;
        if (lWait == WAIT_OBJECT_0) {
            DWORD lExit = 1;
            GetExitCodeProcess(lProcess.hProcess, &lExit);
            lResult = lExit == 0 ? RunResult::eOk : ((lExit == 22 || lExit == 37) ? RunResult::eRejected : RunResult::eFailed);
            break;
        }
        if (pCancel.load()) {
            TerminateProcess(lProcess.hProcess, 1);
            WaitForSingleObject(lProcess.hProcess, 2000);
            lResult = RunResult::eCancelled;
            break;
        }
    }
    CloseHandle(lProcess.hProcess);
    CloseHandle(lProcess.hThread);
    return lResult;
}

#else

RunResult RunDownloader(const std::vector<std::string>& pArguments, const std::string& pOutputPath,
                        const std::atomic<bool>& pCancel, std::atomic<long long>& pBytesDone)
{
    std::vector<char*> lArgv;
    for (const std::string& lArgument : pArguments) {
        lArgv.push_back(const_cast<char*>(lArgument.c_str()));
    }
    lArgv.push_back(nullptr);

    const pid_t lChild = fork();
    if (lChild < 0) {
        return RunResult::eFailed;
    }
    if (lChild == 0) {
        // No shell: the arguments are passed to the program as-is.
        execvp(lArgv[0], lArgv.data());
        _exit(127);
    }

    int lStatus = 0;
    while (true) {
        const pid_t lDone = waitpid(lChild, &lStatus, WNOHANG);
        const long long lSize = FileSize(pOutputPath);
        pBytesDone = lSize > 0 ? lSize : 0;
        if (lDone == lChild) {
            break;
        }
        if (lDone < 0 && errno != EINTR) {
            return RunResult::eFailed;
        }
        if (pCancel.load()) {
            kill(lChild, SIGTERM);
            waitpid(lChild, &lStatus, 0);
            return RunResult::eCancelled;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (WIFEXITED(lStatus)) {
        const int lCode = WEXITSTATUS(lStatus);
        if (lCode == 0) return RunResult::eOk;
        if (lCode == 127) return RunResult::eNotFound;
        // curl --fail exits 22 on an HTTP error; wget exits 8 on a server error response.
        if (pArguments[0] == "curl" ? (lCode == 22 || lCode == 37) : lCode == 8) return RunResult::eRejected;
    }
    return RunResult::eFailed;
}

#endif

} // namespace

std::string DefaultTrackDownloadBaseUrl()
{
    const char* lOverride = std::getenv("HOVERNET_TRACK_DOWNLOAD_URL");
    if (lOverride != nullptr && lOverride[0] != '\0') {
        std::string lBase = lOverride;
        while (!lBase.empty() && lBase.back() == '/') lBase.pop_back();
        return lBase;
    }
    return "https://github.com/johnsie/HoverNet/releases/download";
}

bool MakeDirectories(const std::string& pPath)
{
    std::string lPartial;
    for (std::size_t lIndex = 0; lIndex < pPath.size(); ++lIndex) {
        lPartial += pPath[lIndex];
        const bool lSeparator = pPath[lIndex] == '/' || pPath[lIndex] == '\\';
        if ((lSeparator || lIndex + 1 == pPath.size()) && lPartial.size() > 1) {
#ifdef _WIN32
            _mkdir(lPartial.c_str());
#else
            mkdir(lPartial.c_str(), 0755);
#endif
        }
    }
#ifdef _WIN32
    struct _stat lInfo;
    return _stat(pPath.c_str(), &lInfo) == 0;
#else
    struct stat lInfo;
    return stat(pPath.c_str(), &lInfo) == 0 && S_ISDIR(lInfo.st_mode);
#endif
}

bool DownloadTrack(const TrackEntry& pTrack, const TrackDownloadSettings& pSettings,
                   const std::atomic<bool>& pCancel, std::atomic<long long>& pBytesDone,
                   std::string& pError)
{
    pBytesDone = 0;
    if (!pTrack.mHasDownload || !IsSafeShard(pTrack.mShard) || !IsHex(pTrack.mAsset, 16, 64) ||
        !IsHex(pTrack.mSha256, 64, 64) || !TrackCatalog::IsSafeTrackName(pTrack.mName)) {
        pError = "'" + pTrack.mName + "' has no download available";
        return false;
    }
    if (pSettings.mBaseUrl.empty() || pSettings.mDestination.empty()) {
        pError = "downloads are not configured";
        return false;
    }
    if (!MakeDirectories(pSettings.mDestination)) {
        pError = "cannot create the track folder " + pSettings.mDestination;
        return false;
    }

    const std::string lUrl = pSettings.mBaseUrl + "/" + pTrack.mShard + "/" + pTrack.mAsset + ".trk.gz";
    const std::string lDownloadPath = pSettings.mDestination + pTrack.mAsset + ".part";
    const std::string lUnpackedPath = pSettings.mDestination + pTrack.mAsset + ".tmp";
    const std::string lFinalPath = pSettings.mDestination + pTrack.mName + ".trk";
    auto lCleanup = [&]() {
        std::remove(lDownloadPath.c_str());
        std::remove(lUnpackedPath.c_str());
    };
    std::remove(lDownloadPath.c_str());

#ifdef _WIN32
    const std::vector<std::vector<std::string>> lTools = {
        {"curl.exe", "--fail", "--location", "--silent", "--show-error", "--retry", "2", "--connect-timeout", "15",
         "--max-time", "600", "--output", lDownloadPath, lUrl}};
#else
    const std::vector<std::vector<std::string>> lTools = {
        {"curl", "--fail", "--location", "--silent", "--show-error", "--retry", "2", "--connect-timeout", "15",
         "--max-time", "600", "--output", lDownloadPath, lUrl},
        {"wget", "--quiet", "--tries=3", "--timeout=30", "--output-document", lDownloadPath, lUrl}};
#endif

    RunResult lResult = RunResult::eNotFound;
    for (const std::vector<std::string>& lTool : lTools) {
        lResult = RunDownloader(lTool, lDownloadPath, pCancel, pBytesDone);
        if (lResult != RunResult::eNotFound) {
            break;
        }
    }
    if (lResult == RunResult::eCancelled) {
        lCleanup();
        pError = "download cancelled";
        return false;
    }
    if (lResult == RunResult::eNotFound) {
        lCleanup();
#ifdef _WIN32
        pError = "curl.exe was not found; it ships with Windows 10 and later";
#else
        pError = "neither curl nor wget is installed; install one (for example: sudo apt install curl)";
#endif
        return false;
    }
    if (lResult == RunResult::eRejected) {
        lCleanup();
        pError = "'" + pTrack.mName + "' is not available on the download server yet. "
                 "It may not have been published; try again later or install the offline track pack.";
        return false;
    }
    if (lResult != RunResult::eOk) {
        lCleanup();
        pError = "could not download '" + pTrack.mName + "' - check your internet connection";
        return false;
    }

    std::string lDecodeError;
    if (!GunzipFile(lDownloadPath, lUnpackedPath, kMaxTrackBytes, lDecodeError)) {
        lCleanup();
        pError = "'" + pTrack.mName + "': " + lDecodeError;
        return false;
    }
    std::remove(lDownloadPath.c_str());

    const long long lSize = FileSize(lUnpackedPath);
    if ((pTrack.mBytes > 0 && lSize != pTrack.mBytes) || Sha256::HashFile(lUnpackedPath) != pTrack.mSha256) {
        lCleanup();
        pError = "'" + pTrack.mName + "' failed verification (the download was corrupt or has changed)";
        return false;
    }
    if (!ReplaceFile(lUnpackedPath, lFinalPath)) {
        lCleanup();
        pError = "cannot save '" + pTrack.mName + "' into " + pSettings.mDestination;
        return false;
    }
    pBytesDone = pTrack.mDownloadBytes > 0 ? pTrack.mDownloadBytes : pBytesDone.load();
    return true;
}

TrackDownloadJob::TrackDownloadJob(const TrackDownloadSettings& pSettings, const std::vector<TrackEntry>& pTracks)
    : mSettings(pSettings), mTracks(pTracks)
{
    for (const TrackEntry& lTrack : mTracks) {
        mTotalBytes += lTrack.mDownloadBytes > 0 ? lTrack.mDownloadBytes : 1;
    }
}

TrackDownloadJob::~TrackDownloadJob()
{
    Cancel();
    if (mThread.joinable()) {
        mThread.join();
    }
}

void TrackDownloadJob::Start()
{
    mThread = std::thread([this]() { Run(); });
}

void TrackDownloadJob::Cancel()
{
    mCancelled = true;
}

void TrackDownloadJob::RunBlocking()
{
    Run();
}

void TrackDownloadJob::Run()
{
    for (const TrackEntry& lTrack : mTracks) {
        if (mCancelled.load()) {
            break;
        }
        {
            std::lock_guard<std::mutex> lLock(mMutex);
            mCurrentName = lTrack.mName;
        }
        mCurrentBytes = 0;
        std::string lError;
        std::atomic<long long> lBytes{0};
        // Progress for the current track is mirrored into mCurrentBytes by polling.
        std::atomic<bool> lDone{false};
        std::thread lWatcher([&]() {
            while (!lDone.load()) {
                mCurrentBytes = lBytes.load();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        const bool lOk = DownloadTrack(lTrack, mSettings, mCancelled, lBytes, lError);
        lDone = true;
        lWatcher.join();
        if (!lOk) {
            std::lock_guard<std::mutex> lLock(mMutex);
            mError = lError;
            mCurrentBytes = 0;
            break;
        }
        mFinishedBytes += lTrack.mDownloadBytes > 0 ? lTrack.mDownloadBytes : 1;
        mCurrentBytes = 0;
        ++mCompleted;
        std::lock_guard<std::mutex> lLock(mMutex);
        mInstalled.push_back(lTrack.mName);
    }
    mFinished = true;
}

std::string TrackDownloadJob::CurrentName() const
{
    std::lock_guard<std::mutex> lLock(mMutex);
    return mCurrentName;
}

std::string TrackDownloadJob::Error() const
{
    std::lock_guard<std::mutex> lLock(mMutex);
    return mError;
}

std::vector<std::string> TrackDownloadJob::InstalledNames() const
{
    std::lock_guard<std::mutex> lLock(mMutex);
    return mInstalled;
}
