// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin developers
// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2015-2021 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers
 */
#ifndef PIVX_LOGGING_H
#define PIVX_LOGGING_H

#include "fs.h"
#include "tinyformat.h"

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <vector>


static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS        = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;
extern const char * const DEFAULT_DEBUGLOGFILE;

extern bool fLogIPs;

struct CLogCategoryActive
{
    std::string category;
    bool active;
};

namespace BCLog {
    enum LogFlags : uint32_t {
        NONE        = 0,
        NET         = (1 <<  0),
        TOR         = (1 <<  1),
        MEMPOOL     = (1 <<  2),
        HTTP        = (1 <<  3),
        BENCHMARK   = (1 <<  4),
        ZMQ         = (1 <<  5),
        DB          = (1 <<  6),
        RPC         = (1 <<  7),
        ESTIMATEFEE = (1 <<  8),
        ADDRMAN     = (1 <<  9),
        SELECTCOINS = (1 << 10),
        REINDEX     = (1 << 11),
        CMPCTBLOCK  = (1 << 12),
        RANDOM      = (1 << 13),
        PRUNE       = (1 << 14),
        PROXY       = (1 << 15),
        MEMPOOLREJ  = (1 << 16),
        LIBEVENT    = (1 << 17),
        COINDB      = (1 << 18),
        QT          = (1 << 19),
        LEVELDB     = (1 << 20),
        STAKING     = (1 << 21),
        MASTERNODE  = (1 << 22),
        MNBUDGET    = (1 << 23),
        MNPING      = (1 << 24),
        LEGACYZC    = (1 << 25),
        SAPLING     = (1 << 26),
        SPORKS      = (1 << 27),
        LLMQ        = (1 << 28),
        DKG         = (1 << 29),
        ALL         = ~(uint32_t)0,
    };

    class Logger
    {
    private:
        FILE* m_fileout = nullptr;
        std::mutex m_file_mutex;
        std::list<std::string> m_msgs_before_open;

        /**
         * m_started_new_line is a state variable that will suppress printing of
         * the timestamp when multiple calls are made that don't end in a
         * newline.
         */
        std::atomic_bool m_started_new_line{true};

        /** Log categories bitfield. */
        std::atomic<uint32_t> m_categories{0};

        std::string LogTimestampStr(const std::string& str);

    public:
        bool m_print_to_console = false;
        bool m_print_to_file = false;

        bool m_log_timestamps = DEFAULT_LOGTIMESTAMPS;
        bool m_log_time_micros = DEFAULT_LOGTIMEMICROS;

        fs::path m_file_path;
        std::atomic<bool> m_reopen_file{false};

        /** Send a string to the log output */
        void LogPrintStr(const std::string &str);

        /** Returns whether logs will be written to any output */
        bool Enabled() const { return m_print_to_console || m_print_to_file; }

        bool OpenDebugLog();
        void ShrinkDebugFile();

        uint32_t GetCategoryMask() const { return m_categories.load(); }

        void EnableCategory(LogFlags flag);
        bool EnableCategory(const std::string& str);
        void DisableCategory(LogFlags flag);
        bool DisableCategory(const std::string& str);

        bool WillLogCategory(LogFlags category) const;

        bool DefaultShrinkDebugFile() const;
    };

} // namespace BCLog

extern BCLog::Logger* const g_logger;

/** Return true if log accepts specified category */
static inline bool LogAcceptCategory(BCLog::LogFlags category)
{
    return g_logger->WillLogCategory(category);
}

/** Returns a string with the supported log categories */
std::string ListLogCategories();

/** Returns a vector of the active log categories. */
std::vector<CLogCategoryActive> ListActiveLogCategories();

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str);

/** Get format string from VA_ARGS for error reporting */
template<typename... Args> std::string FormatStringFromLogArgs(const char *fmt, const Args&... args) { return fmt; }

// Be conservative when using LogPrintf/error or other things which
// unconditionally log to debug.log! It should not be the case that an inbound
// peer can fill up a user's disk with debug.log entries.

#define LogPrintf(...) do {                                                         \
    if(g_logger->Enabled()) {                                                       \
        std::string _log_msg_; /* Unlikely name to avoid shadowing variables */     \
        try {                                                                       \
            _log_msg_ = tfm::format(__VA_ARGS__);                                   \
        } catch (tinyformat::format_error &e) {                                     \
            /* Original format string will have newline so don't add one here */    \
            _log_msg_ = "Error \"" + std::string(e.what()) +                        \
                        "\" while formatting log message: " +                       \
                        FormatStringFromLogArgs(__VA_ARGS__);                       \
        }                                                                           \
        g_logger->LogPrintStr(_log_msg_);                                           \
    }                                                                               \
} while(0)

#define LogPrint(category, ...) do {                                                \
    if (LogAcceptCategory((category))) {                                            \
        LogPrintf(__VA_ARGS__);                                                     \
    }                                                                               \
} while(0)

/// PIVX

class CBatchedLogger
{
private:
    BCLog::Logger* logger;
    bool accept;
    std::string header;
    std::string msg;
public:
    CBatchedLogger(BCLog::LogFlags _category, const std::string& _header);
    virtual ~CBatchedLogger();
    void Flush();

    template<typename... Args>
    void Batch(const std::string& fmt, const Args&... args)
    {
        if (!accept) {
            return;
        }
        msg += "    " + strprintf(fmt, args...) + "\n";
    }
};

#endif // PIVX_LOGGING_H
