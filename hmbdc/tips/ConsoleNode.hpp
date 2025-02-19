#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/tips/Bag.hpp"

#include <boost/format.hpp>

#include <unordered_set>
#include <iostream>
#include <iomanip>
#include <memory>
#include <unistd.h>
#include <atomic>
#include <optional>

namespace hmbdc::tips {
/**
 * @class ConsoleNode<>
 * @brief a Node that work as a console to send and receive messages
 * @details a Node that receives all messages delivered to the process
 *  it uses JustBytes to indicate it wants all the messages bytes
 * regardless of their types
 * 
 * @tparam Domain some NetProp type for a specific transport mechanism, 
 */
struct ConsoleNode 
: Node<ConsoleNode, std::tuple<app::JustBytes>, std::tuple<app::JustBytes>, will_schedule_timer> {
    /**
     * @brief constructor
     * @details just construct the Node - need to be run in the Context later
     * 
     * @param domain a runtime sized context that the Node will run in
     * @param myCin for incoming commands 
     * @param myCout for output
     * @param myCerr for error
     */
    ConsoleNode(app::Config const& cfg
        , std::istream& myCin =  std::cin
        , std::ostream& myCout = std::cout
        , std::ostream& myCerr = std::cerr) 
    : config_(cfg)
    , myCin_(myCin)
    , myCout_(myCout)
    , myCerr_(myCerr)
    , bufWidth_(cfg.getExt<size_t>("bufWidth"))
    , toSend_(new uint8_t[bufWidth_]) {
        std::thread stdinThread([this]() {
            stdinThreadEntrance();
        });
        stdinThread_ = std::move(stdinThread);
    }

    char const* hmbdcName() const { 
        return "console"; 
    }

    size_t maxMessageSize() const {
        return bufWidth_;
    }

    /**
     * @brief documentation for all commands the console interprets
     * @details commands are seperated by \n, when read eof, terminates console
     * @return a documentation string 
     */
    static char const* help() {
        return 
R"|(CONSOLE LANGUAGE:
This console accept control cmds and messages from stdin and send output to stdout. stderr has errors.
Pub/sub cmds and example:
pubtags <space-seperated-tags>
    example: pubtags 1001 1004 1002
subtags <space-seperated-tags>
    example: subtags 2001 1004 11002
pubstr <tag> <string>
    example: pubstr 1099 some string in a line
pub <tag> <msg-len> <space-seprated-hex-for-msg>
    example: pub 1251 4 a1 a2 a3 a4
pubbin <tag> <msg-len> <after the newline, followed by msg-len binary bytes>
    example: pubbin 1251 4
    /*4 undisplayable binary bytes */
    used by pipe tools only, pub is recommened over pubbin due to safety
pubatt <tag> <msg-len> <attachment-len> <space-seprated-hex-for-msg-followed-by-attachment>
    example: pubatt 1201 4 10 01 02 03 04 01 02 03 04 05 06 07 08 09 0a
pubattbin <tag> <msg-len> <attachment-len> <after the newline, followed by msg-len + att-len binary bytes>
    example: pubattbin 1201 4 10
    /*14 undisplayable binary bytes */
    used by pipe tools only, pubatt is recommened over pubattbin due to safety
play <bag-file-name>
    play a recorded data bag (in the same relative timing as recorded)

Output control cmds:
ohex
    output bytes in hex - default. There is always a '\n' between the message bytes and attachment bytes.
    ohex output example:
    1201 msgatt= 02 03 04 ff ff ff ff ff ff ff ff ff ff ff
    att= 01 02 03 04 05 06 07 08 09 0a

ostr
    output in string - unless you are sure all the incoming messages are strings without attachment, do not use this.
    ostr output example:
    1099 msgstr= some string in a line

obin
    output binary bytes - this only works well when connecting console with pipes, not recommended normally
    obin output example:
    1099 msgbin= 1024,0
    /* after the newline, followed by 1024 binary bytes */
    1201 msgattbin= 1016 10 
    /* after the newline, followed by 1016 + 10 binary bytes */

record <bag-file-name> <duration>
    record the output within the next duration seconds with time info in binary bag format into a file (bag) and exit
    example:
    record /tmp/a.bag 99.5

Other cmds:
exit
    exit

)|"
;
    }

    void stoppedCb(std::exception const& e) override {
        myCerr_ << "[status] " << (e.what()) << std::endl;
    };

    void addJustBytesPubsForCfg(std::function<void(uint16_t)> addTag) const {
        for (auto tag : pubTags_) {
            addTag(tag);
        }
    }

    void addJustBytesSubsForCfg(std::function<void(uint16_t)> addTag) const {
        for (auto tag : subTags_) {
            addTag(tag);
        }
    }

    void invokedCb(size_t) override {
        if (recordBagOpen_ && !recordBagCloseTimer_.scheduled()) {
            this->schedule(time::SysTime::now() + recordDuration_, recordBagCloseTimer_);
        }

        if (toPlayTag_ == 0 && playBagOpen_ && !playBagFireTimer_.scheduled()) {
            /// schedule for the first time
            schedule(time::SysTime::now(), playBagFireTimer_);
        }
    }

    void handleJustBytesCb(uint16_t tag, uint8_t const* bytes, app::hasMemoryAttachment* att) {
        //only handle user msgs
        if (tag <= app::LastSystemMessage::typeTag) return;

        if (recordBag_) {
            recordBag_->record(tag, bytes, att);
        } else {
            if (att) {
                bytes += sizeof(app::hasMemoryAttachment);
            }
            if (outputForm_ == OHEX) {
                myCout_ << std::dec << tag << (att?" msgatt=":" msg=");
                for (auto p = bytes; p != bytes + bufWidth_ - sizeof(app::hasMemoryAttachment); ++p) {
                    myCout_ << ' ' << boost::format("%02x") % (uint16_t)*p;
                }
                if (att) {
                    myCout_ << "\natt=";
                    for (auto p = (uint8_t*)att->attachment; p != (uint8_t*)att->attachment + att->len; ++p) {
                        myCout_ << ' ' << boost::format("%02x") % (uint16_t)*p;
                    }
                    att->release(); /// important for JustBytes
                }
            } else if (outputForm_ == OSTR)  {
                myCout_ << std::dec << tag << " msgstr=";
                myCout_ << ' ' << std::string((char*)bytes, strnlen((char*)bytes, bufWidth_));
                if (att) {
                    myCerr_ << "[status] ignored att for tag=" << tag << std::endl;
                }
            } else if (outputForm_ == OBIN)  {
                if (!att) {
                    auto msgLen = bufWidth_;
                    myCout_ << std::dec << tag << " msgbin= " << msgLen << '\n';
                    myCout_.write((char const*)bytes, msgLen);
                } else {
                    auto msgLen = bufWidth_ - sizeof(app::hasMemoryAttachment);
                    auto attLen = att->len;
                    myCout_ << std::dec << tag << " msgattbin= " << msgLen << ' ' << attLen << '\n';
                    myCout_.write((char const*)bytes, msgLen);
                    myCout_.write((char const*)att->attachment, attLen);
                    att->release(); /// important for JustBytes
                }
            }
            myCout_ << std::endl;
        }
    }

    /**
     * @brief wait until the console closes
     */
    void waitUntilFinish() {
        stdinThread_.join();
    }

 private:
    enum OForm {
        OHEX,
        OSTR,
        OBIN
    };
    
    std::atomic<OForm> outputForm_{OHEX};
    void stdinThreadEntrance() {
        myCerr_ << "[status] Session started" << std::endl;
        processCmd(myCin_);
        myCerr_ << "[status] Session stopped" << std::endl;
    }

    void processCmd(std::istream& is) {
        while(is) {
            std::string line;
            std::getline(is, line);
            if (!is){
                if (!is.eof()) {
                    myCerr_ << "input error, reset input" << std::endl;
                    is.clear();
                    is.ignore();
                    continue;
                } else {
                    break;
                }
            }
            if (!line.size()) continue;
            std::istringstream iss{line};
            std::string op;
            iss >> op;
            if (op == "pubtags") {
                for (auto it = std::istream_iterator<uint16_t>(iss)
                    ; it != std::istream_iterator<uint16_t>()
                    ; ++it) {
                    pubTags_.insert(*it);
                };
                resetPubSub();
            } else if (op == "subtags") {
                for (auto it = std::istream_iterator<uint16_t>(iss)
                    ; it != std::istream_iterator<uint16_t>()
                    ; ++it) {
                    subTags_.insert(*it);
                    this->subscription.set(*it);
                };
                resetPubSub();
            } else if (op == "pubstr") {
                uint16_t tag;
                iss >> tag;
                std::string msg;
                std::getline(iss, msg); /// msg has a leading space
                auto len = msg.size();
                if (iss && len >= 0 && pubTags_.find(tag) != pubTags_.end()) {
                    publishJustBytes(tag, msg.c_str() + 1, (size_t)len, nullptr);
                } else {
                    myCerr_ << "[status] pubstr syntax error or unknown tag in line:" << line << std::endl;
                }
            } else if (op == "pub") {
                uint16_t tag;
                iss >> tag;
                size_t msgLen;
                iss >> msgLen;
                iss >> std::hex;
                if (msgLen > bufWidth_) {
                    myCerr_ << "[status] msgLen too big" << std::endl;
                    continue;
                }
                auto msgBytes = &toSend_[0];
                for (auto i = 0u; iss && i < msgLen; ++i) {
                    uint16_t byte;
                    iss >> byte;
                    msgBytes[i] = (uint8_t)byte;
                };
                if (iss && pubTags_.find(tag) != pubTags_.end()) {
                    publishJustBytes(tag, toSend_.get(), msgLen, nullptr);
                } else {
                    myCerr_ << "[status] syntax error or unknown tag in line:" << line << std::endl;
                }
            } else if (op == "pubbin") {
                uint16_t tag;
                iss >> tag;
                size_t msgLen;
                iss >> msgLen;
                if (msgLen > bufWidth_) {
                    myCerr_ << "[status] msgLen too big" << std::endl;
                    continue;
                }
                auto msgBytes = &toSend_[0];
                is.read((char*)msgBytes, msgLen);
                if (iss && pubTags_.find(tag) != pubTags_.end()) {
                    publishJustBytes(tag, toSend_.get(), msgLen, nullptr);
                } else {
                    myCerr_ << "[status] syntax error or unknown tag in line:" << line << std::endl;
                }
            } else if (op == "pubatt") {
                uint16_t tag;
                iss >> tag;
                size_t msgLen, attLen;
                iss >> msgLen >> attLen;
                iss >> std::hex;
                auto att = new (toSend_.get()) app::hasMemoryAttachment;
                if (msgLen + sizeof(*att) > bufWidth_) {
                    myCerr_ << "[status] msgLen too big" << std::endl;
                    continue;
                }
                auto msgBytes = &toSend_[sizeof(*att)];
                for (auto i = 0u; iss && i < msgLen; ++i) {
                    uint16_t byte;
                    iss >> byte;
                    msgBytes[i] = (uint8_t)byte;
                };
                if (iss) {
                    att->len = attLen;
                    att->attachment = malloc(sizeof(size_t) + att->len);
                    auto& refCount = *(size_t*)att->attachment;
                    /// based on what we know set the release called times
                    /// to be 2 or 1
                    refCount = this->subscription.check(tag) ? 2 : 1;
                    att->clientData[0] = (uint64_t)&refCount;
                    att->attachment = (char*)att->attachment + sizeof(size_t);
                    att->afterConsumedCleanupFunc = [](app::hasMemoryAttachment* att) {
                        auto& refCount = *((size_t*)att->clientData[0]);
                        
                        if (0 == --*reinterpret_cast<std::atomic<size_t>*>(&refCount)) {
                        // if (0 == __atomic_sub_fetch(&refCount, 1, __ATOMIC_RELAXED)) {
                            auto toFree = (char*)att->attachment;
                            toFree -= sizeof(size_t);
                            ::free(toFree);
                        }
                    };

                    for (auto i = 0u; iss && i < attLen; ++i) {
                        uint16_t byte;
                        iss >> byte;
                        ((uint8_t*)att->attachment)[i] = (uint8_t)byte;
                    };
                }
                if (iss && pubTags_.find(tag) != pubTags_.end()) {
                    publishJustBytes(tag, toSend_.get(), msgLen + sizeof(*att), att);
                } else {
                    myCerr_ << "[status] syntax error or unknown tag in line:" << line << std::endl;
                    auto toFree = (char*)att->attachment;
                    toFree -= sizeof(size_t);
                    ::free(toFree);
                    // att->release();
                }
            } else if (op == "pubattbin") {
                uint16_t tag;
                iss >> tag;
                size_t msgLen, attLen;
                iss >> msgLen >> attLen;
                iss >> std::hex;
                auto att = new (toSend_.get()) app::hasMemoryAttachment;
                if (msgLen + sizeof(*att) > bufWidth_) {
                    myCerr_ << "[status] msgLen too big" << std::endl;
                    continue;
                }
                auto msgBytes = &toSend_[sizeof(*att)];
                is.read((char*)msgBytes, msgLen);
                if (iss) {
                    att->len = attLen;
                    att->attachment = malloc(sizeof(size_t) + att->len);
                    auto& refCount = *(size_t*)att->attachment;
                    /// based on what we know set the release called times
                    /// to be 2 or 1
                    refCount = this->subscription.check(tag) ? 2 : 1;
                    att->clientData[0] = (uint64_t)&refCount;
                    att->attachment = (char*)att->attachment + sizeof(size_t);
                    att->afterConsumedCleanupFunc = [](app::hasMemoryAttachment* att) {
                        auto& refCount = *((size_t*)att->clientData[0]);
                        
                        if (0 == --*reinterpret_cast<std::atomic<size_t>*>(&refCount)) {
                            auto toFree = (char*)att->attachment;
                            toFree -= sizeof(size_t);
                            ::free(toFree);
                        }
                    };
                    is.read((char*)att->attachment, attLen);
                }
                if (iss && pubTags_.find(tag) != pubTags_.end()) {
                    publishJustBytes(tag, toSend_.get(), msgLen + sizeof(*att), att);
                } else {
                    myCerr_ << "[status] syntax error or unknown tag in line:" << line << std::endl;
                    auto toFree = (char*)att->attachment;
                    toFree -= sizeof(size_t);
                    ::free(toFree);
                    // att->release();
                }
            } else if (op == "record") {
                std::string bagName;
                iss >> bagName >> recordDuration_;
                if (!iss) {
                    myCerr_ << "[status] syntax error " << std::endl;
                } else if (recordBagOpen_) {
                    myCerr_ << "[status] error, already recording! " << std::endl;
                } else {
                    recordBag_.emplace(bagName.c_str(), config_);
                    recordBagOpen_ = *recordBag_;
                    if (!recordBagOpen_) {
                        myCerr_ << "[status] error openning bag! " << std::endl;
                    }
                }
            } else if (op == "play") {
                std::string bagName;
                iss >> bagName;
                if (!iss) {
                    myCerr_ << "[status] syntax error " << std::endl;
                } else if (playBagOpen_) {
                    myCerr_ << "[status] error, already playing! " << std::endl;
                } else {
                    playBag_.emplace(bagName.c_str());
                    playBagPreviousTs_ = playBag_->bagHead().createdTs;
                    toPlayLen_ = playBag_->bufWidth();
                    if (toPlayLen_ > bufWidth_) {
                        myCerr_ << "[status] this console's bufWidth < " << toPlayLen_ 
                            << " cannot play this bag " << std::endl;
                        playBag_->close();
                        continue;
                    }
                    toPlay_.reset(new uint8_t[toPlayLen_]);
                    playBagOpen_ = *playBag_;
                }
            } else if (op == "ohex") {
                outputForm_ = OHEX;
            } else if (op == "ostr") {
                outputForm_ = OSTR;
            } else if (op == "obin") {
                outputForm_ = OBIN;
            } else if (op == "help") {
                myCout_ << help() << std::endl;
            } else if (op == "exit") {
                myCerr_ << "[status] exiting... " << std::endl;
                break;
            } else {
                myCerr_ << "[status] unknown command " << op << std::endl;
            }
        }
        if (recordBagOpen_) {
            recordBag_->close();
            myCerr_ << "[status] command input closed, record bag ready! bag stats:\n";
            recordBag_->dumpStats(myCerr_);
            myCerr_ << std::endl;
        }
    }

    app::Config config_;
    std::thread stdinThread_;
    std::istream& myCin_;
    std::ostream& myCout_;
    std::ostream& myCerr_;
    std::optional<OutputBag> recordBag_;
    time::Duration recordDuration_;
    std::atomic<bool> recordBagOpen_ = false;
    time::OneTimeTimer recordBagCloseTimer_{[this](auto&& tm, auto&& now){
        recordBag_->close();
        recordBagOpen_ = false;
        myCerr_ << "[status] recorded in bag:" << std::endl;
        for (auto const& [tag, count] : recordBag_->stats()) {
            myCerr_ << "[status] " << tag << " : " << count << std::endl;
        }
        myCerr_ << "[status] record bag ready! hit ctrl-d to exit" << std::endl;
        throw 0;
    }};

    std::optional<InputBag> playBag_;
    time::SysTime playBagPreviousTs_;
    std::atomic<bool> playBagOpen_ = false;
    time::ReoccuringTimer playBagFireTimer_{time::Duration::seconds(0)
        , [this](auto&& tm, auto&& firedAt) {
            if (toPlayTag_) {
                publishJustBytes(
                    toPlayTag_, toPlay_.get(), toPlayLen_, toPlayAtt_);
            }
            auto bmh = playBag_->play(toPlayTag_, toPlay_.get(), toPlayAtt_);
            if (*playBag_) {
                playBagFireTimer_.resetInterval(bmh.createdTs - playBagPreviousTs_);
                playBagPreviousTs_ = bmh.createdTs;
                if (toPlayAtt_) {
                    auto& refCount = *(size_t*)toPlayAtt_->clientData[0];
                    /// based on what we know set the release called times
                    /// to be 2 or 1
                    refCount = this->subscription.check(bmh.tag) ? 2 : 1;
                }
            } else {
                if (playBag_->eof()) {
                    myCerr_ << "[status] bag play done " << std::endl;
                } else {
                    myCerr_ << "[status] IO error when reading message " << std::endl;
                }
                playBag_->close();
                playBagOpen_ = false;
                tm.cancel(playBagFireTimer_);
                toPlayTag_ = 0;
            }
        }
    };

    std::unordered_set<uint16_t> subTags_;
    std::unordered_set<uint16_t> pubTags_;
    size_t bufWidth_;
    std::unique_ptr<uint8_t[]> toSend_;

    uint16_t toPlayTag_ = 0;
    size_t toPlayLen_ = 0;
    std::unique_ptr<uint8_t[]> toPlay_;
    app::hasMemoryAttachment* toPlayAtt_ = nullptr;
};

}
