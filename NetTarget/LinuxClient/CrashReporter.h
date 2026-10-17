// CrashReporter.h -- a last-gasp crash report for the dedicated server: on a fatal signal (or, on Windows, an unhandled exception)
// it writes a short text file with what happened and where, then lets the process die as it would have. The operator collects the
// files from the crash directory; nothing is sent anywhere.
#ifndef CRASH_REPORTER_H
#define CRASH_REPORTER_H

#include <string>

namespace CrashReporter
{
// dir must exist. version and what are copied into the report.
void Install(const std::string& dir, const std::string& version);
}

#endif
